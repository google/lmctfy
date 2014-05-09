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

#include "lmctfy/resources/memory_resource_handler.h"

#include <memory>
#include <string>
using ::std::string;
#include <vector>

#include "base/integral_types.h"
#include "base/logging.h"
#include "lmctfy/resource_handler.h"
#include "include/lmctfy.pb.h"
#include "util/safe_types/bytes.h"
#include "util/safe_types/unix_gid.h"
#include "util/safe_types/unix_uid.h"
#include "util/errors.h"
#include "strings/substitute.h"
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
using ::strings::Substitute;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace lmctfy {

static const int32 kDefaultEvictionPriority = 5000;
static const int32 kMinEvictionPriority = 0;
static const int32 kMaxEvictionPriority = 10000;
static const int32 kDefaultDirtyRatio = 75;
static const int32 kDefaultDirtyBackgroundRatio = 10;
static const Bytes kDefaultDirtyLimit = Bytes(0);
static const Bytes kDefaultDirtyBackgroundLimit = Bytes(0);

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
  StatusOr<MemoryController *> statusor =
      memory_controller_factory_->Create(container_name);
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

// TODO(vmarmol): Move this elsewhere to be used by other files that need it.
// Ignores errors of NOT_FOUND.
Status IgnoreNotFound(const Status &status) {
  if (!status.ok() && status.error_code() != ::util::error::NOT_FOUND) {
    return status;
  }

  return Status::OK;
}

Status MemoryResourceHandler::CreateOnlySetup(const ContainerSpec &spec) {
  // TODO(rgooch): make this configurable.
  // Some kernels do not support setting stale page age, ignore in those cases.
  return IgnoreNotFound(memory_controller_->SetStalePageAge(1));
}

Status MemoryResourceHandler::SetDirty(const MemorySpec_Dirty &dirty,
                                       Container::UpdatePolicy policy) {
  const bool setting_ratio = dirty.has_ratio() || dirty.has_background_ratio();
  const bool setting_limit = dirty.has_limit() || dirty.has_background_limit();

  // First do error checking and make sure only one type is used.
  if (setting_ratio && setting_limit) {
    return Status(
        ::util::error::INVALID_ARGUMENT,
        "Cannot set both dirty ratio and limit");
  }

  // Don't require both ratio/bg_ratio or limit/bg_limit together, as it's
  // possible for just one of the two to be changing.  e.g., if we have 0 bg
  // ratio but are changing the ratio, the SetDirty update will only include
  // the ratio value and not the bg value.
  if (dirty.has_ratio()) {
    RETURN_IF_ERROR(memory_controller_->SetDirtyRatio(dirty.ratio()));
  }
  if (dirty.has_background_ratio()) {
    RETURN_IF_ERROR(memory_controller_->SetDirtyBackgroundRatio(
        dirty.background_ratio()));
  }
  if (dirty.has_limit()) {
    RETURN_IF_ERROR(memory_controller_->SetDirtyLimit(Bytes(dirty.limit())));
  }
  if (dirty.has_background_limit()) {
    RETURN_IF_ERROR(memory_controller_->SetDirtyBackgroundLimit(
        Bytes(dirty.background_limit())));
  }

  // Set any that need to be set which aren't included
  if (policy == Container::UPDATE_REPLACE) {
    // If we're not setting limits, we should be setting ratio (if neither is
    // requested, default to ratio).
    if (!setting_limit) {
      if (!dirty.has_ratio()) {
        RETURN_IF_ERROR(IgnoreNotFound(
            memory_controller_->SetDirtyRatio(kDefaultDirtyRatio)));
      }
      if (!dirty.has_background_ratio()) {
        RETURN_IF_ERROR(
            IgnoreNotFound(memory_controller_->SetDirtyBackgroundRatio(
                kDefaultDirtyBackgroundRatio)));
      }
    } else {
      if (!dirty.has_limit()) {
        RETURN_IF_ERROR(IgnoreNotFound(
            memory_controller_->SetDirtyLimit(kDefaultDirtyLimit)));
      }
      if (!dirty.has_background_limit()) {
        RETURN_IF_ERROR(
            IgnoreNotFound(memory_controller_->SetDirtyBackgroundLimit(
                kDefaultDirtyBackgroundLimit)));
      }
    }
  }
  return Status::OK;
}

Status MemoryResourceHandler::Update(const ContainerSpec &spec,
                                     Container::UpdatePolicy policy) {
  const MemorySpec &memory_spec = spec.memory();

  // Set the OOM score if it was specified.
  if (memory_spec.has_eviction_priority()) {
    const int64 eviction_priority = memory_spec.eviction_priority();

    // Check usage.
    if (eviction_priority < kMinEvictionPriority ||
        eviction_priority > kMaxEvictionPriority) {
      return Status(
          ::util::error::INVALID_ARGUMENT,
          Substitute(
              "Eviction priority of $0 is outside valid range of $1-$2",
              eviction_priority, kMinEvictionPriority, kMaxEvictionPriority));
    }

    Status status = memory_controller_->SetOomScore(eviction_priority);
    // TODO(jnagal): Fix after adding support for GetFeatures().
    if (!status.ok() && status.error_code() != ::util::error::NOT_FOUND) {
      return status;
    }
  } else if (policy == Container::UPDATE_REPLACE) {
    // OOM score may not be supported in all kernels so don't fail if it is not
    // supported and not specified.
    Status status = memory_controller_->SetOomScore(kDefaultEvictionPriority);
    if (!status.ok() && status.error_code() != ::util::error::NOT_FOUND) {
      return status;
    }
  }

  // Set the limit. The default is -1 if it was not specified during a replace.
  if (memory_spec.has_limit()) {
    RETURN_IF_ERROR(memory_controller_->SetLimit(Bytes(memory_spec.limit())));
  } else if (policy == Container::UPDATE_REPLACE) {
    RETURN_IF_ERROR(memory_controller_->SetLimit(Bytes(-1)));
  }

  // Set the swap limit if it was specified. The default is -1 if it was not
  // specified during a replace.
  // TODO(zohaib): swap_limit must be greater than or equal to the limit. We
  // need to check that this is true.
  if (memory_spec.has_swap_limit()) {
    RETURN_IF_ERROR(
        memory_controller_->SetSwapLimit(Bytes(memory_spec.swap_limit())));
  } else if (policy == Container::UPDATE_REPLACE) {
    Status status = memory_controller_->SetSwapLimit(Bytes(-1));
    // This may not be supported in all kernels so don't fail if it is not
    // supported and not specified.
    if (!status.ok() && status.error_code() != ::util::error::NOT_FOUND) {
      return status;
    }
  }

  // Set the reservation if it was specified. The default is 0 if it was not
  // specified during a replace.
  if (memory_spec.has_reservation()) {
    RETURN_IF_ERROR(
        memory_controller_->SetSoftLimit(Bytes(memory_spec.reservation())));
  } else if (policy == Container::UPDATE_REPLACE) {
    RETURN_IF_ERROR(memory_controller_->SetSoftLimit(Bytes(0)));
  }

  // Set the compression sampling ratio.
  if (memory_spec.has_compression_sampling_ratio()) {
    RETURN_IF_ERROR(memory_controller_->SetCompressionSamplingRatio(
        memory_spec.compression_sampling_ratio()));
  } else if (policy == Container::UPDATE_REPLACE) {
    Status status = memory_controller_->SetCompressionSamplingRatio(0);
    // This may not be supported in all kernels so don't fail if it is not
    // supported and not specified.
    if (!status.ok() && status.error_code() != ::util::error::NOT_FOUND) {
      return status;
    }
  }

  // Set the stale page age.
  if (memory_spec.has_stale_page_age()) {
    RETURN_IF_ERROR(memory_controller_->SetStalePageAge(
        memory_spec.stale_page_age()));
  } else if (policy == Container::UPDATE_REPLACE) {
    // This may not be supported in all kernels so don't fail if it is not
    // supported and not specified.
    RETURN_IF_ERROR(IgnoreNotFound(memory_controller_->SetStalePageAge(1)));
  }

  // Set dirty [background] ratio/limit data
  RETURN_IF_ERROR(SetDirty(memory_spec.dirty(), policy));

  if (memory_spec.has_kmem_charge_usage()) {
    RETURN_IF_ERROR(memory_controller_->SetKMemChargeUsage(
        memory_spec.kmem_charge_usage()));
  } else if (policy == Container::UPDATE_REPLACE) {
    // This may not be supported in all kernels so don't fail if it is not
    // supported and not specified.
    RETURN_IF_ERROR(IgnoreNotFound(
        memory_controller_->SetKMemChargeUsage(false)));
  }

  return Status::OK;
}

Status MemoryResourceHandler::Stats(Container::StatsType type,
                                    ContainerStats *output) const {
  MemoryStats *stats = output->mutable_memory();
  Status any_failure = Status::OK;

  // TODO(jonathanw): limit and reservation are spec, not stats; remove them
  // from Stats since they're returned in Spec.
  SET_IF_PRESENT_VAL_SAVE_FAILURE(
      memory_controller_->GetWorkingSet(), stats->set_working_set, any_failure);
  SET_IF_PRESENT_VAL_SAVE_FAILURE(
      memory_controller_->GetUsage(), stats->set_usage, any_failure);
  SET_IF_PRESENT_VAL_SAVE_FAILURE(
      memory_controller_->GetMaxUsage(), stats->set_max_usage, any_failure);
  SET_IF_PRESENT_VAL_SAVE_FAILURE(
      memory_controller_->GetLimit(), stats->set_limit, any_failure);
  SET_IF_PRESENT_VAL_SAVE_FAILURE(
      memory_controller_->GetEffectiveLimit(),
      stats->set_effective_limit, any_failure);
  SET_IF_PRESENT_VAL_SAVE_FAILURE(
      memory_controller_->GetSoftLimit(), stats->set_reservation, any_failure);
  SET_IF_PRESENT_SAVE_FAILURE(
      memory_controller_->GetFailCount(), stats->set_fail_count, any_failure);

  SAVE_IF_ERROR(memory_controller_->GetMemoryStats(stats), any_failure);
  SAVE_IF_ERROR(memory_controller_->GetNumaStats(stats->mutable_numa()),
                any_failure);
  SAVE_IF_ERROR(IgnoreNotFound(
      memory_controller_->GetIdlePageStats(stats->mutable_idle_page())),
      any_failure);
  SAVE_IF_ERROR(memory_controller_->GetCompressionSamplingStats(
                    stats->mutable_compression_sampling()),
                any_failure);

  return any_failure;
}

// Ratio gets preference over limits.
// As per our current memcg interface, we expect both limit and ratio to be
// exported and be greater than or equal to zero.
// TODO(kyurtsever, vishnuk): Error out if either limit or ratio is not set or
// is lesser than 0.
Status MemoryResourceHandler::GetDirtyMemorySpec(
    MemorySpec *memory_spec) const {
  auto dirty_spec = memory_spec->mutable_dirty();
  SET_IF_PRESENT(memory_controller_->GetDirtyRatio(),
                 dirty_spec->set_ratio);
  SET_IF_PRESENT_VAL(memory_controller_->GetDirtyLimit(),
                     dirty_spec->set_limit);
  if (dirty_spec->limit() > 0) {
    dirty_spec->clear_ratio();
  } else {
    dirty_spec->clear_limit();
  }

  SET_IF_PRESENT(memory_controller_->GetDirtyBackgroundRatio(),
                 dirty_spec->set_background_ratio);
  SET_IF_PRESENT_VAL(memory_controller_->GetDirtyBackgroundLimit(),
                     dirty_spec->set_background_limit);

  if (dirty_spec->background_limit() > 0) {
    dirty_spec->clear_background_ratio();
  } else {
    dirty_spec->clear_background_limit();
  }
  return Status::OK;
}

Status MemoryResourceHandler::Spec(ContainerSpec *spec) const {
  MemorySpec *memory_spec = spec->mutable_memory();

  SET_IF_PRESENT(memory_controller_->GetOomScore(),
                 memory_spec->set_eviction_priority);
  memory_spec->set_limit(
      RETURN_IF_ERROR(memory_controller_->GetLimit()).value());
  memory_spec->set_reservation(
      RETURN_IF_ERROR(memory_controller_->GetSoftLimit()).value());
  SET_IF_PRESENT(memory_controller_->GetCompressionSamplingRatio(),
                 memory_spec->set_compression_sampling_ratio);
  SET_IF_PRESENT(memory_controller_->GetStalePageAge(),
                 memory_spec->set_stale_page_age);
  SET_IF_PRESENT(memory_controller_->GetKMemChargeUsage(),
                 memory_spec->set_kmem_charge_usage);
  RETURN_IF_ERROR(GetDirtyMemorySpec(memory_spec));
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
