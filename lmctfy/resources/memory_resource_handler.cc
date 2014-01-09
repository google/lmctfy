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

#include "lmctfy/resources/memory_resource_handler.h"

#include <memory>
#include <string>
using ::std::string;
#include <vector>

#include "base/integral_types.h"
#include "base/logging.h"
#include "util/bytes.h"
#include "lmctfy/resource_handler.h"
#include "include/lmctfy.pb.h"
#include "util/safe_types/unix_gid.h"
#include "util/safe_types/unix_uid.h"
#include "util/errors.h"
#include "util/task/codes.pb.h"
#include "util/task/status.h"

namespace containers {
namespace lmctfy {
class CgroupController;
}  // namespace lmctfy
}  // namespace containers

using ::util::Bytes;
using ::util::UnixGid;
using ::util::UnixUid;
using ::std::unique_ptr;
using ::std::vector;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace lmctfy {

StatusOr<MemoryResourceHandlerFactory *> MemoryResourceHandlerFactory::New(
    CgroupFactory *cgroup_factory, const KernelApi *kernel,
    EventFdNotifications *eventfd_notifications) {
  // Memory hierarchy must be mounted.
  if (!cgroup_factory->IsMounted(MemoryControllerFactory::HierarchyType())) {
    return Status(::util::error::NOT_FOUND,
                  "Memory resource depends on the memory cgroup hierarchy");
  }

  // Create memory controller.
  MemoryControllerFactory *memory_controller = new MemoryControllerFactory(
      cgroup_factory, kernel, eventfd_notifications);

  return new MemoryResourceHandlerFactory(memory_controller, cgroup_factory,
                                          kernel);
}

MemoryResourceHandlerFactory::MemoryResourceHandlerFactory(
    const MemoryControllerFactory *memory_controller_factory,
    CgroupFactory *cgroup_factory, const KernelApi *kernel)
    : CgroupResourceHandlerFactory(RESOURCE_MEMORY, cgroup_factory, kernel),
      memory_controller_factory_(memory_controller_factory) {}

StatusOr<ResourceHandler *> MemoryResourceHandlerFactory::GetResourceHandler(
    const string &container_name) const {
  // Memory has a 1:1 mapping from container name to hierarchy path. It also
  // only has the memory cgroup controller for now.
  StatusOr<MemoryController *> statusor =
      memory_controller_factory_->Get(container_name);
  if (!statusor.ok()) {
    return statusor.status();
  }

  return new MemoryResourceHandler(container_name, kernel_,
                                   statusor.ValueOrDie());
}

StatusOr<ResourceHandler *> MemoryResourceHandlerFactory::CreateResourceHandler(
    const string &container_name, const ContainerSpec &spec) const {
  // Memory has a 1:1 mapping from container name to hierarchy path. It also
  // only has the memory cgroup controller for now.
  StatusOr<MemoryController *> statusor = memory_controller_factory_->Create(
      container_name, UnixUid(spec.owner()), UnixGid(spec.owner_group()));
  if (!statusor.ok()) {
    return statusor.status();
  }

  return new MemoryResourceHandler(container_name, kernel_,
                                   statusor.ValueOrDie());
}

MemoryResourceHandler::MemoryResourceHandler(
    const string &container_name, const KernelApi *kernel,
    MemoryController *memory_controller)
    : CgroupResourceHandler(container_name, RESOURCE_MEMORY, kernel,
                            vector<CgroupController *>({memory_controller})),
      memory_controller_(CHECK_NOTNULL(memory_controller)) {}

Status MemoryResourceHandler::CreateOnlySetup(const ContainerSpec &spec) {
  // TODO(rgooch): make this configurable.
  return memory_controller_->SetStalePageAge(1);
}

Status MemoryResourceHandler::Update(const ContainerSpec &spec,
                                     Container::UpdatePolicy policy) {
  const MemorySpec &memory_spec = spec.memory();

  // During a diff: update what was specified.
  // During an update:
  // - If no limit specified, default to -1 (unlimited).
  // - If no reservation specified, default to 0.

  // Set the limit. The default is -1 if it was not specified during a replace.
  if (memory_spec.has_limit()) {
    RETURN_IF_ERROR(memory_controller_->SetLimit(Bytes(memory_spec.limit())));
  } else if (policy == Container::UPDATE_REPLACE) {
    RETURN_IF_ERROR(memory_controller_->SetLimit(Bytes(-1)));
  }

  // Set the reservation if it was specified. The default is 0 if it was not
  // specified during a replace.
  if (memory_spec.has_reservation()) {
    RETURN_IF_ERROR(
        memory_controller_->SetSoftLimit(Bytes(memory_spec.reservation())));
  } else if (policy == Container::UPDATE_REPLACE) {
    RETURN_IF_ERROR(memory_controller_->SetSoftLimit(Bytes(0)));
  }

  return Status::OK;
}

Status MemoryResourceHandler::Stats(Container::StatsType type,
                                    ContainerStats *output) const {
  MemoryStats *stats = output->mutable_memory();

  // TODO(jonathanw): limit and reservation are spec, not stats; remove them
  // from Stats since they're returned in Spec.
  SET_IF_PRESENT_VAL(memory_controller_->GetWorkingSet(),
                     stats->set_working_set);
  SET_IF_PRESENT_VAL(memory_controller_->GetUsage(), stats->set_usage);
  SET_IF_PRESENT_VAL(memory_controller_->GetMaxUsage(), stats->set_max_usage);
  SET_IF_PRESENT_VAL(memory_controller_->GetLimit(), stats->set_limit);
  SET_IF_PRESENT_VAL(memory_controller_->GetEffectiveLimit(),
                     stats->set_effective_limit);
  SET_IF_PRESENT_VAL(memory_controller_->GetSoftLimit(),
                     stats->set_reservation);

  return Status::OK;
}

Status MemoryResourceHandler::Spec(ContainerSpec *spec) const {
  MemorySpec *memory_spec = spec->mutable_memory();
  {
    Bytes limit;
    RETURN_IF_ERROR(memory_controller_->GetLimit(), &limit);
    memory_spec->set_limit(limit.value());
  }
  {
    Bytes reservation;
    RETURN_IF_ERROR(memory_controller_->GetSoftLimit(), &reservation);
    memory_spec->set_reservation(reservation.value());
  }
  return Status::OK;
}

StatusOr<Container::NotificationId> MemoryResourceHandler::RegisterNotification(
    const EventSpec &spec, Callback1<Status> *callback) {
  unique_ptr<Callback1<Status>> callback_deleter(callback);

  if (spec.has_oom() && spec.has_memory_threshold()) {
    // TODO(vmarmol): Consider doing this check in ContainerImpl with proto
    // introspection.
    return Status(::util::error::INVALID_ARGUMENT,
                  "Can only register notifications for one event at a time");
  }

  // OOM event.
  if (spec.has_oom()) {
    return memory_controller_->RegisterOomNotification(
        callback_deleter.release());
  }

  // Memory threshold event.
  if (spec.has_memory_threshold()) {
    // Ensure there is a threshold.
    if (!spec.memory_threshold().has_usage()) {
      return Status(::util::error::INVALID_ARGUMENT,
                    "Memory threshold event must specify a usage threshold");
    }

    return memory_controller_->RegisterUsageThresholdNotification(
        Bytes(spec.memory_threshold().usage()), callback_deleter.release());
  }

  // No known event found.
  return Status(::util::error::NOT_FOUND, "No handled event found");
}

}  // namespace lmctfy
}  // namespace containers
