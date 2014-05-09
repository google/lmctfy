// Copyright 2014 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "lmctfy/resources/cpu_resource_handler.h"

#include <string>
using ::std::string;
#include <utility>
#include <vector>

#include "base/integral_types.h"
#include "base/logging.h"
#include "file/base/file.h"
#include "file/base/path.h"
#include "lmctfy/controllers/cgroup_controller.h"
#include "lmctfy/controllers/cpu_controller.h"
#include "lmctfy/controllers/cpuacct_controller.h"
#include "lmctfy/controllers/cpuset_controller.h"
#include "lmctfy/resource_handler.h"
#include "include/lmctfy.pb.h"
#include "util/cpu_mask.h"
#include "util/errors.h"
#include "strings/substitute.h"
#include "util/gtl/stl_util.h"
#include "util/task/codes.pb.h"

using ::file::JoinPath;
using ::containers::InitSpec;
using ::util::CpuMask;
using ::std::unique_ptr;
using ::std::vector;
using ::strings::Substitute;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace lmctfy {

// Identifier for the batch subsystem.
const char kBatchSubsystem[] = "/batch";

StatusOr<CpuResourceHandlerFactory *> CpuResourceHandlerFactory::New(
    CgroupFactory *cgroup_factory, const KernelApi *kernel,
    EventFdNotifications *eventfd_notifications) {
  // Cpu and CpuAcct hierarchies must be mounted.
  if (!cgroup_factory->IsMounted(CpuControllerFactory::HierarchyType())) {
    return Status(::util::error::NOT_FOUND,
                  "CPU resource depends on the cpu cgroup hierarchy");
  }
  if (!cgroup_factory->IsMounted(CpuAcctControllerFactory::HierarchyType())) {
    return Status(::util::error::NOT_FOUND,
                  "CPU resource depends on the cpuacct cgroup hierarchy");
  }

  // Create Cpu and CpuAcct controller factories.
  CpuControllerFactory *cpu_controller =
      new CpuControllerFactory(cgroup_factory, kernel, eventfd_notifications);
  CpuAcctControllerFactory *cpuacct_controller = new CpuAcctControllerFactory(
      cgroup_factory, kernel, eventfd_notifications);

  // Cpuset is only used if available.
  CpusetControllerFactory *cpuset_controller = nullptr;
  if (cgroup_factory->IsMounted(CpusetControllerFactory::HierarchyType())) {
    cpuset_controller = new CpusetControllerFactory(cgroup_factory, kernel,
                                                    eventfd_notifications);
  }

  return new CpuResourceHandlerFactory(cpu_controller, cpuacct_controller,
                                       cpuset_controller, cgroup_factory,
                                       kernel);
}

// Gets the CPU hierarchy path of the specified container.
// In the hierarchical CPU world:
// For LS containers:
// - /alloc          -> /alloc
// - /alloc/task     -> /alloc/task
// - /alloc/task/sub -> /alloc/task/sub
// - /task           -> /task
// - /task/sub       -> /task/sub
// For Batch containers:
// - /alloc          -> /batch/alloc
// - /alloc/task     -> /batch/task
// - /alloc/task/sub -> /batch/task/sub
// - /task           -> /batch/task
// - /task/sub       -> /batch/task/sub
//
// In the non-hierarchical CPU world:
// For LS containers:
// - /alloc          -> /alloc
// - /alloc/task     -> /task
// - /alloc/task/sub -> /task/sub
// - /task           -> /task
// - /task/sub       -> /task/sub
// For Batch containers:
// - /alloc          -> /batch/alloc
// - /alloc/task     -> /batch/task
// - /alloc/task/sub -> /batch/task/sub
// - /task           -> /batch/task
// - /task/sub       -> /batch/task/sub
//
// Note that batch's behavior does not change as hierarchical CPU only applies
// to LS tasks. Batch is always non-hierarchical except for subcontainers which
// are *always* under their parent.
StatusOr<string> GetCpuHierarchyPath(const CpuControllerFactory *controller,
                                     const string &container_name) {
  // The above configurations are handled by 2 mappings:
  // 1. Identity map the container name to / or /batch.
  // 2. Remove the base container and map to / or /batch).
  //    The base container is "/foo" in "/foo/bar/baz".

  // Mapping 1.
  //
  // We first check whether the full container is at / or /batch. This happens
  // for top-level tasks, their subcontainers, allocs, LS tasks inside allocs
  // when hierarchical CPU is enabled, and their subcontainers.
  if (controller->Exists(container_name)) {
    return container_name;
  }
  const string batch_path = JoinPath(kBatchSubsystem, container_name);
  if (controller->Exists(batch_path)) {
    return batch_path;
  }

  // Mapping 2.
  //
  // The remaining possibilities are the things that are non-hierarchical. This
  // is comprised of tasks inside allocs and their subcontainers. They are
  // either LS (although non-hierarchical as those are handled above) or batch.
  // Since these are non-hierarchical, we must first strip the base container.
  size_t second_slash = container_name.find_first_of('/', 1);
  if (second_slash != string::npos) {
    // TODO(vmarmol): Remove this first when Hierarchical CPU is enabled. The
    // second batch check should remain.
    const string base_container_name = container_name.substr(second_slash);
    if (controller->Exists(base_container_name)) {
      return base_container_name;
    }

    const string base_batch_path =
        JoinPath(kBatchSubsystem, base_container_name);
    if (controller->Exists(base_batch_path)) {
      return base_batch_path;
    }
  }

  // The container was not found under any path, it must not exist.
  return Status(
      ::util::error::NOT_FOUND,
      Substitute("Did not find container \"$0\" in cpu cgroup hierarchy",
                 container_name));
}

CpuResourceHandlerFactory::CpuResourceHandlerFactory(
    const CpuControllerFactory *cpu_controller_factory,
    const CpuAcctControllerFactory *cpuacct_controller_factory,
    const CpusetControllerFactory *cpuset_controller_factory,
    CgroupFactory *cgroup_factory, const KernelApi *kernel)
    : CgroupResourceHandlerFactory(RESOURCE_CPU, cgroup_factory, kernel),
      cpu_controller_factory_(cpu_controller_factory),
      cpuacct_controller_factory_(cpuacct_controller_factory),
      cpuset_controller_factory_(cpuset_controller_factory) {}

StatusOr<ResourceHandler *> CpuResourceHandlerFactory::GetResourceHandler(
    const string &container_name) const {
  // Get the hierarchy paths for cpu and cpuacct.
  string cpu_hierarchy_path = RETURN_IF_ERROR(
      GetCpuHierarchyPath(cpu_controller_factory_.get(), container_name));

  // Cpu and cpuacct have the same hierarchy and depend on the type of job.
  unique_ptr<CpuController> cpu_controller(
      RETURN_IF_ERROR(cpu_controller_factory_->Get(cpu_hierarchy_path)));
  unique_ptr<CpuAcctController> cpuacct_controller(
      RETURN_IF_ERROR(cpuacct_controller_factory_->Get(cpu_hierarchy_path)));

  // Only create cpuset if available.
  unique_ptr<CpusetController> cpuset_controller;
  if (cpuset_controller_factory_ != nullptr) {
    // Cpuset is flat.
    cpuset_controller.reset(RETURN_IF_ERROR(cpuset_controller_factory_->Get(
        JoinPath("/", file::Basename(container_name).ToString()))));
  }

  return new CpuResourceHandler(container_name, kernel_,
                                cpu_controller.release(),
                                cpuacct_controller.release(),
                                cpuset_controller.release());
}

// TODO(vmarmol): Be able to create non-hierarchical LS CPU if that is
// specified.
StatusOr<ResourceHandler *> CpuResourceHandlerFactory::CreateResourceHandler(
    const string &container_name, const ContainerSpec &spec) const {
  const string base_container_name = file::Basename(container_name).ToString();
  const string parent_name = file::Dirname(container_name).ToString();

  // TODO(vmarmol): Support creation of batch subcontainers under non-batch
  // top-level containers.

  // Get the hierarchy paths for cpu and cpuacct.
  string cpu_hierarchy_path;
  if (parent_name == "/") {
    // For top-level containers, we place batch in /batch and on top-level
    // otherwise. Batch are those with scheduling_latency of NORMAL
    // or BEST_EFFORT. PRIORITY is the default.
    if (!spec.cpu().has_scheduling_latency() ||
        (spec.cpu().scheduling_latency() == PRIORITY) ||
        (spec.cpu().scheduling_latency() == PREMIER)) {
      cpu_hierarchy_path = container_name;
    } else {
      cpu_hierarchy_path = JoinPath(kBatchSubsystem, container_name);
    }
  } else {
    // For subcontainers the following logic always creates cpu and cpuacct
    // cgroups under the parent path irrespective of latency setting.
    cpu_hierarchy_path = RETURN_IF_ERROR(
        GetCpuHierarchyPath(cpu_controller_factory_.get(), parent_name));
    cpu_hierarchy_path = JoinPath(cpu_hierarchy_path, base_container_name);
  }

  unique_ptr<CpuController> cpu_controller(
      RETURN_IF_ERROR(cpu_controller_factory_->Create(cpu_hierarchy_path)));
  unique_ptr<CpuAcctController> cpuacct_controller(
      RETURN_IF_ERROR(cpuacct_controller_factory_->Create(cpu_hierarchy_path)));

  // Only create cpuset if available.
  unique_ptr<CpusetController> cpuset_controller;
  if (cpuset_controller_factory_ != nullptr) {
    // Cpuset is flat.
    cpuset_controller.reset(RETURN_IF_ERROR(cpuset_controller_factory_->Create(
        JoinPath("/", file::Basename(base_container_name).ToString()))));
  }

  return new CpuResourceHandler(container_name, kernel_,
                                cpu_controller.release(),
                                cpuacct_controller.release(),
                                cpuset_controller.release());
}

Status CpuResourceHandlerFactory::InitMachine(const InitSpec &spec) {
  // TODO(jnagal): Init Machine actions:
  // Create NUMA cpuset cgroups based on InitSpec.
  // Initialize histograms: CpuAcctController::EnableSchedulerHistograms()

  // Create the batch subsystem in cpu and cpuacct. It is okay if they already
  // exist since InitMachine() should be idempotent.
  unique_ptr<CpuController> cpu_controller;
  {
    StatusOr<CpuController *> statusor =
        cpu_controller_factory_->Create(kBatchSubsystem);
    if (statusor.ok()) {
      cpu_controller.reset(statusor.ValueOrDie());
    } else if (statusor.status().error_code() !=
               ::util::error::ALREADY_EXISTS) {
      return statusor.status();
    } else {
      cpu_controller.reset(
          RETURN_IF_ERROR(cpu_controller_factory_->Get(kBatchSubsystem)));
    }
  }
  unique_ptr<CpuAcctController> cpuacct_controller;
  {
    StatusOr<CpuAcctController *> statusor =
        cpuacct_controller_factory_->Create(kBatchSubsystem);
    if (statusor.ok()) {
      cpuacct_controller.reset(statusor.ValueOrDie());
    } else if (statusor.status().error_code() !=
               ::util::error::ALREADY_EXISTS) {
      return statusor.status();
    } else {
      cpuacct_controller.reset(
          RETURN_IF_ERROR(cpuacct_controller_factory_->Get(kBatchSubsystem)));
    }
  }

  // Give the batch subsystem the minimum amount of CPU so it only uses CPU when
  // available.
  RETURN_IF_ERROR(cpu_controller->SetMilliCpus(0));

  // Setup histogram. Histograms may not be supported..
  Status status = cpuacct_controller->SetupHistograms();
  if (!status.ok() && status.error_code() != ::util::error::NOT_FOUND) {
    return status;
  }

  // If available, set cpuset to inherit from the parent. We do this for root
  // and that is inherited by its children.
  if (cpuset_controller_factory_ != nullptr) {
    unique_ptr<CpusetController> cpuset_controller(
        RETURN_IF_ERROR(cpuset_controller_factory_->Get("/")));
    RETURN_IF_ERROR(cpuset_controller->EnableCloneChildren());
  }

  return Status::OK;
}

// Creates a vector with only the available controllers.
vector<CgroupController *> PackControllers(
    CpuController *cpu_controller, CpuAcctController *cpuacct_controller,
    CpusetController *cpuset_controller) {
  if (cpuset_controller == nullptr) {
    return {cpu_controller, cpuacct_controller};
  }

  return {cpu_controller, cpuacct_controller, cpuset_controller};
}

CpuResourceHandler::CpuResourceHandler(const string &container_name,
                                       const KernelApi *kernel,
                                       CpuController *cpu_controller,
                                       CpuAcctController *cpuacct_controller,
                                       CpusetController *cpuset_controller)
    : CgroupResourceHandler(container_name, RESOURCE_CPU, kernel,
                            PackControllers(cpu_controller, cpuacct_controller,
                                            cpuset_controller)),
      cpu_controller_(CHECK_NOTNULL(cpu_controller)),
      cpuacct_controller_(CHECK_NOTNULL(cpuacct_controller)),
      cpuset_controller_(cpuset_controller) {}

Status CpuResourceHandler::CreateOnlySetup(const ContainerSpec &spec) {
  // Setup latency before calling update. Ignore if latency is not supported.
  if (spec.has_cpu()) {
    SchedulingLatency latency = PRIORITY;
    if (spec.cpu().has_scheduling_latency()) {
      latency = spec.cpu().scheduling_latency();
    }
    Status status = cpu_controller_->SetLatency(latency);
    if (!status.ok() && status.error_code() != ::util::error::NOT_FOUND) {
      return status;
    }
  }

  // Setup Histogram buckets. Histograms may not be supported.
  Status status = cpuacct_controller_->SetupHistograms();
  if (!status.ok() && status.error_code() != ::util::error::NOT_FOUND) {
    return status;
  }

  // TODO(jnagal): Set placement strategy.
  return Status::OK;
}

Status CpuResourceHandler::Update(const ContainerSpec &spec,
                                  Container::UpdatePolicy policy) {
  if (!spec.has_cpu()) {
    return Status::OK;
  }

  const CpuSpec &cpu_spec = spec.cpu();

  // Get latency, don't fail if it was not found/supported.
  StatusOr<SchedulingLatency> statusor = cpu_controller_->GetLatency();
  if (!statusor.ok() &&
      statusor.status().error_code() != ::util::error::NOT_FOUND) {
    return statusor.status();
  }

  // Latency setting cannot be updated.
  // We only care about switching between batch and LS latencies, but a blanket
  // ban is probably easier to track. Ignore this logic if CPU latency is not
  // supported (NOT_FOUND).
  if (statusor.ok()) {
    if (cpu_spec.has_scheduling_latency() &&
        cpu_spec.scheduling_latency() != statusor.ValueOrDie()) {
      return Status(::util::error::INVALID_ARGUMENT,
                    "Cannot change latency setting.");
    }

    // Check if the default latency is being changed through a replace.
    if (policy == Container::UPDATE_REPLACE &&
        !cpu_spec.has_scheduling_latency() &&
        statusor.ValueOrDie() != PRIORITY) {
      return Status(::util::error::INVALID_ARGUMENT,
                    "Cannot change latency setting.");
    }
  }

  // Is there a throughput specified if policy is UPDATE_REPLACE.
  if (!cpu_spec.has_limit() && policy == Container::UPDATE_REPLACE) {
    // TODO(jnagal): Do we override the value to default (1024) or fail the
    // request?
    // Ignore for now.
  }

  // Set throughput.
  if (cpu_spec.has_limit()) {
    RETURN_IF_ERROR(cpu_controller_->SetMilliCpus(cpu_spec.limit()));
  }

  // Set max throughput.
  if (cpu_spec.has_max_limit()) {
    RETURN_IF_ERROR(cpu_controller_->SetMaxMilliCpus(cpu_spec.max_limit()));
  }

  // Set affinity mask.
  if (cpu_spec.has_mask()) {
    if (cpuset_controller_ == nullptr) {
      return Status(::util::error::INVALID_ARGUMENT,
                    "Setting CPU masks is not supported on this configuration");
    }

    RETURN_IF_ERROR(
        cpuset_controller_->SetCpuMask(CpuMask(cpu_spec.mask().data())));
  }

  return Status::OK;
}

Status CpuResourceHandler::Stats(Container::StatsType type,
                                 ContainerStats *output) const {
  CpuStats *cpu_stats = output->mutable_cpu();

  // Cpu usage.
  SET_IF_PRESENT(cpuacct_controller_->GetCpuUsageInNs(),
                 cpu_stats->mutable_usage()->set_total);

  // Cpu load.
  SET_IF_PRESENT(cpu_controller_->GetNumRunnable(), cpu_stats->set_load);

  {
    auto set_cpu_time = [&cpu_stats](const CpuTime &cpu_time) {
      cpu_stats->mutable_usage()->set_user(cpu_time.user.value());
      cpu_stats->mutable_usage()->set_system(cpu_time.system.value());
    };
    SET_IF_PRESENT(cpuacct_controller_->GetCpuTime(), set_cpu_time);
  }

  {
    auto set_per_cpu = [&cpu_stats](vector<int64> *per_cpu) {
      ::std::copy(per_cpu->begin(),
                  per_cpu->end(),
                  RepeatedFieldBackInserter(
                      cpu_stats->mutable_usage()->mutable_per_cpu()));
      delete per_cpu;
    };
    SET_IF_PRESENT(cpuacct_controller_->GetPerCpuUsageInNs(),
                   set_per_cpu);
  }

  // Stats below this check are only returned for STATS_FULL.
  if (type == Container::STATS_SUMMARY) {
    return Status::OK;
  }

  // Throttling Stats.
  // Throttling stats are not included in summary as they are only relevant when
  // max_limit are specified.
  {
    StatusOr<ThrottlingStats> statusor = cpu_controller_->GetThrottlingStats();
    if (statusor.ok()) {
      ThrottlingData *data = cpu_stats->mutable_throttling_data();
      data->set_periods(statusor.ValueOrDie().nr_periods);
      data->set_throttled_periods(statusor.ValueOrDie().nr_throttled);
      data->set_throttled_time(statusor.ValueOrDie().throttled_time);
    } else if (statusor.status().error_code() != ::util::error::NOT_FOUND) {
      return statusor.status();
    }
  }

  // Scheduling Histograms.
  // This assumes that the histograms were setup during Create().
  {
    StatusOr<vector<CpuHistogramData *> *> statusor =
        cpuacct_controller_->GetSchedulerHistograms();
    if (statusor.ok()) {
      unique_ptr<vector<CpuHistogramData *>> histograms(statusor.ValueOrDie());
      ElementDeleter d(histograms.get());
      for (auto histogram_data : *histograms) {
        HistogramMap *histogram = cpu_stats->add_histograms();
        histogram->set_type(histogram_data->type);
        for (auto data : histogram_data->buckets) {
          HistogramMap_Bucket *bucket = histogram->add_stat();
          bucket->set_bucket(data.first);
          bucket->set_value(data.second);
        }
      }
    } else if (statusor.status().error_code() != ::util::error::NOT_FOUND) {
      return statusor.status();
    }
  }

  return Status::OK;
}

Status CpuResourceHandler::Spec(ContainerSpec *spec) const {
  {
    int64 milli_cpus =
        RETURN_IF_ERROR(cpu_controller_->GetMilliCpus());
    spec->mutable_cpu()->set_limit(milli_cpus);
  }
  {
    int64 max_milli_cpus =
        RETURN_IF_ERROR(cpu_controller_->GetMaxMilliCpus());
    spec->mutable_cpu()->set_max_limit(max_milli_cpus);
  }
  if (cpuset_controller_ != nullptr) {
    CpuMask cpu_mask =
        RETURN_IF_ERROR(cpuset_controller_->GetCpuMask());
    cpu_mask.WriteToProtobuf(
        spec->mutable_cpu()->mutable_mask()->mutable_data());
  }
  return Status::OK;
}

StatusOr<Container::NotificationId> CpuResourceHandler::RegisterNotification(
    const EventSpec &spec, Callback1<Status> *callback) {
  // TODO(jnagal): Evaluate if we need kernel notifications for cpu.
  // Most of the notifications can be handled in userspace.
  unique_ptr<Callback1<Status>> d(callback);
  return Status(::util::error::NOT_FOUND, "No supported notifications for CPU");
}
}  // namespace lmctfy
}  // namespace containers
