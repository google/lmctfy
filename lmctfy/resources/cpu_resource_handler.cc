// Copyright 2013 Google Inc. All Rights Reserved.
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
#include "lmctfy/controllers/cpuset_controller_stub.h"
#include "lmctfy/resource_handler.h"
#include "include/lmctfy.pb.h"
#include "util/errors.h"
#include "strings/substitute.h"
#include "util/gtl/stl_util.h"
#include "util/os/core/cpu_set.h"
#include "util/task/codes.pb.h"

using ::file::JoinPath;
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
  // Cpu, CpuAcct, and cpuset hierarchies must be mounted.
  if (!cgroup_factory->IsMounted(CpuControllerFactory::HierarchyType())) {
    return Status(::util::error::NOT_FOUND,
                  "CPU resource depends on the cpu cgroup hierarchy");
  }
  if (!cgroup_factory->IsMounted(CpuAcctControllerFactory::HierarchyType())) {
    return Status(::util::error::NOT_FOUND,
                  "CPU resource depends on the cpuacct cgroup hierarchy");
  }
  if (!cgroup_factory->IsMounted(CpusetControllerFactory::HierarchyType())) {
    return Status(::util::error::NOT_FOUND,
                  "CPU resource depends on the cpuset cgroup hierarchy");
  }

  // Create controllers.
  bool owns_cpu =
      cgroup_factory->OwnsCgroup(CpuControllerFactory::HierarchyType());
  bool owns_cpuacct =
      cgroup_factory->OwnsCgroup(CpuAcctControllerFactory::HierarchyType());
  bool owns_cpuset =
      cgroup_factory->OwnsCgroup(CpusetControllerFactory::HierarchyType());
  CpuControllerFactory *cpu_controller = new CpuControllerFactory(
      cgroup_factory, owns_cpu, kernel, eventfd_notifications);
  CpuAcctControllerFactory *cpuacct_controller = new CpuAcctControllerFactory(
      cgroup_factory, owns_cpuacct, kernel, eventfd_notifications);
  CpusetControllerFactory *cpuset_controller = new CpusetControllerFactory(
      cgroup_factory, owns_cpuset, kernel, eventfd_notifications);

  return new CpuResourceHandlerFactory(cpu_controller, cpuacct_controller,
                                       cpuset_controller, cgroup_factory,
                                       kernel);
}

// Gets the CPU hierarchy path of the specified controller and container. This
// may be /batch/<container name> or /<container name> depending on how the
// container was set up.
StatusOr<string> GetCpuHierarchyPath(const CpuControllerFactory *controller,
                                     const string &container_name) {
  // Check if the container exists under either /batch or /.
  const string batch_path = JoinPath(kBatchSubsystem, container_name);
  if (controller->Exists(container_name)) {
    return container_name;
  } else if (controller->Exists(batch_path)) {
    return batch_path;
  } else {
    return Status(
        ::util::error::NOT_FOUND,
        Substitute("Did not find container \"$0\" in cpu cgroup hierarchy",
                   container_name));
  }
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
  string cpu_hierarchy_path;
  RETURN_IF_ERROR(
      GetCpuHierarchyPath(cpu_controller_factory_.get(), container_name),
      &cpu_hierarchy_path);

  // Cpu and cpuacct have the same hierarchy and depend on the type of job.
  CpuController *cpu_controller = nullptr;
  RETURN_IF_ERROR(cpu_controller_factory_->Get(cpu_hierarchy_path),
                  &cpu_controller);
  CpuAcctController *cpuacct_controller = nullptr;
  RETURN_IF_ERROR(cpuacct_controller_factory_->Get(cpu_hierarchy_path),
                  &cpuacct_controller);

  // Cpuset is flat.
  StatusOr<CpusetController *> cpuset_statusor =
      cpuset_controller_factory_->Get(
          JoinPath("/", File::Basename(container_name)));
  CpusetController *cpuset_controller;
  if (!cpuset_statusor.ok()) {
    // cpuset hierarchy might not exist in case of subcontainers.
    // Use a stub instead.
    cpuset_controller = new CpusetControllerStub(container_name);
  } else {
    cpuset_controller = cpuset_statusor.ValueOrDie();
  }

  return new CpuResourceHandler(container_name, kernel_, cpu_controller,
                                cpuacct_controller, cpuset_controller);
}

StatusOr<ResourceHandler *> CpuResourceHandlerFactory::Create(
    const string &container_name, const ContainerSpec &spec) {
  // cpu resource handler does not use the default
  // CgroupResourceHandlerFactory::Create().
  // Some operations need to be performed only at Create. We need to sanitize
  // update config in case of UPDATE_REPLACE, but not for Create.
  // Some of the container parameters like scheduling latency cannot be changed.
  // A real update needs to verify those.
  unique_ptr<ResourceHandler> cpu_handler;
  RETURN_IF_ERROR(CreateResourceHandler(container_name, spec), &cpu_handler);
  RETURN_IF_ERROR(cpu_handler->Create(spec));

  return cpu_handler.release();
}

StatusOr<ResourceHandler *> CpuResourceHandlerFactory::CreateResourceHandler(
    const string &container_name, const ContainerSpec &spec) const {
  const string base_container_name = File::Basename(container_name);
  const string parent_name = File::StripBasename(container_name);

  // TODO(vmarmol): Support creation of batch subcontainers under non-batch
  // top-level containers.

  // Get the hierarchy paths for cpu and cpuacct.
  string cpu_hierarchy_path;
  if (parent_name == "/") {
    // For top-level containers, we place batch in /batch and on top-level
    // otherwise. Batch are those with scheduling_latency of NORMAL (the
    // default) or BEST_EFFORT.
    if (!spec.cpu().has_scheduling_latency() ||
        (spec.cpu().scheduling_latency() == BEST_EFFORT) ||
        (spec.cpu().scheduling_latency() == NORMAL)) {
      cpu_hierarchy_path = JoinPath(kBatchSubsystem, container_name);
    } else {
      cpu_hierarchy_path = container_name;
    }
  } else {
    // For subcontaines the following logic always creates cpu and cpuacct
    // cgroups under the parent path irrespective of latency setting.
    RETURN_IF_ERROR(
        GetCpuHierarchyPath(cpu_controller_factory_.get(), parent_name),
        &cpu_hierarchy_path);
    cpu_hierarchy_path = JoinPath(cpu_hierarchy_path, base_container_name);
  }

  CpuController *cpu_controller = nullptr;
  RETURN_IF_ERROR(cpu_controller_factory_->Create(cpu_hierarchy_path),
                  &cpu_controller);
  CpuAcctController *cpuacct_controller = nullptr;
  RETURN_IF_ERROR(cpuacct_controller_factory_->Create(cpu_hierarchy_path),
                  &cpuacct_controller);

  // Cpuset is a flat hierarchy. Use a stub if creation failed.
  string name = JoinPath("/", base_container_name);
  StatusOr<CpusetController *> cpuset_statusor =
      cpuset_controller_factory_->Create(name);
  CpusetController *cpuset_controller = nullptr;
  if (!cpuset_statusor.ok()) {
    cpuset_controller = new CpusetControllerStub(name);
  } else {
    cpuset_controller = cpuset_statusor.ValueOrDie();
  }

  return new CpuResourceHandler(container_name, kernel_, cpu_controller,
                                cpuacct_controller, cpuset_controller);
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
      RETURN_IF_ERROR(cpu_controller_factory_->Get(kBatchSubsystem),
                      &cpu_controller);
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
      RETURN_IF_ERROR(cpuacct_controller_factory_->Get(kBatchSubsystem),
                      &cpuacct_controller);
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

  return Status::OK;
}

CpuResourceHandler::CpuResourceHandler(
    const string &container_name, const KernelApi *kernel,
    CpuController *cpu_controller, CpuAcctController *cpuacct_controller,
    CpusetController *cpuset_controller)
    : CgroupResourceHandler(container_name, RESOURCE_CPU, kernel,
                            vector<CgroupController *>({
                              cpu_controller,
                              cpuacct_controller,
                              cpuset_controller})),
    cpu_controller_(CHECK_NOTNULL(cpu_controller)),
    cpuacct_controller_(CHECK_NOTNULL(cpuacct_controller)),
    cpuset_controller_(CHECK_NOTNULL(cpuset_controller)) {}

Status CpuResourceHandler::Create(const ContainerSpec &spec) {
  // Setup latency before calling update. Ignore if latency is not supported.
  if (spec.has_cpu() && spec.cpu().has_scheduling_latency()) {
    Status status =
        cpu_controller_->SetLatency(spec.cpu().scheduling_latency());
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
  return Update(spec, Container::UPDATE_REPLACE);
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
        !cpu_spec.has_scheduling_latency() && statusor.ValueOrDie() != NORMAL) {
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
    RETURN_IF_ERROR(
        cpu_controller_->SetMaxMilliCpus(cpu_spec.max_limit()));
  }

  // Set affinity mask.
  if (cpu_spec.has_mask()) {
    RETURN_IF_ERROR(cpuset_controller_->SetCpuMask(
        ::util_os_core::UInt64ToCpuSet(cpu_spec.mask())));
  } else if (policy == Container::UPDATE_REPLACE) {
    // Nothing set so inherit from parent. This is necessary for upstream
    // kernels that don't do this by default (by default they leave the field
    // blank).
    RETURN_IF_ERROR(cpuset_controller_->InheritCpuMask());
  }

  // Inherit memory nodes from parent. This is necessary for upstream kernels
  // that don't do this by default (by default they leave the field blank).
  if (policy == Container::UPDATE_REPLACE) {
    RETURN_IF_ERROR(cpuset_controller_->InheritMemoryNodes());
  }

  return Status::OK;
}

Status CpuResourceHandler::Stats(Container::StatsType type,
                                 ContainerStats *output) const {
  CpuStats *cpu_stats = output->mutable_cpu();

  // Cpu usage.
  SET_IF_PRESENT(cpuacct_controller_->GetCpuUsageInNs(), cpu_stats->set_usage);

  // Cpu load.
  SET_IF_PRESENT(cpu_controller_->GetNumRunnable(), cpu_stats->set_load);

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
      vector<CpuHistogramData *> *histograms = statusor.ValueOrDie();
      for (auto histogram_data : *histograms) {
        HistogramMap *histogram = cpu_stats->add_histograms();
        histogram->set_type(histogram_data->type);
        for (auto data : histogram_data->buckets) {
          HistogramMap_Bucket *bucket = histogram->add_stat();
          bucket->set_bucket(data.first);
          bucket->set_value(data.second);
        }
      }
      STLDeleteElements(histograms);
    } else if (statusor.status().error_code() != ::util::error::NOT_FOUND) {
      return statusor.status();
    }
  }

  return Status::OK;
}

Status CpuResourceHandler::Spec(ContainerSpec *spec) const {
  {
    int64 milli_cpus;
    RETURN_IF_ERROR(cpu_controller_->GetMilliCpus(), &milli_cpus);
    spec->mutable_cpu()->set_limit(milli_cpus);
  }
  {
    int64 max_milli_cpus;
    RETURN_IF_ERROR(cpu_controller_->GetMaxMilliCpus(), &max_milli_cpus);
    spec->mutable_cpu()->set_max_limit(max_milli_cpus);
  }
  // TODO(jonathanw): Populate cpu mask; util_os_core doesn't appear to have
  // an inverse for UInt64ToCpuSet, making this more difficult.
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
