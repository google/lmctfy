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

#include "system_api/kernel_api.h"
#include "lmctfy/controllers/cgroup_factory.h"
#include "lmctfy/controllers/eventfd_notifications.h"
#include "lmctfy/resource_handler.h"
#include "lmctfy/resources/cpu_resource_handler.h"
#include "lmctfy/resources/device_resource_handler.h"
#include "lmctfy/resources/memory_resource_handler.h"
#include "lmctfy/resources/monitoring_resource_handler.h"
#include "util/errors.h"
#include "util/task/status.h"
#include "util/task/statusor.h"

using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace lmctfy {

// Appends the factory to output if the status is OK. Ignores those with status
// NOT_FOUND.
static Status AppendIfAvailable(
    const StatusOr<ResourceHandlerFactory *> &statusor,
    vector<ResourceHandlerFactory *> *output) {
  if (statusor.ok()) {
    output->push_back(statusor.ValueOrDie());
  } else if (statusor.status().error_code() != ::util::error::NOT_FOUND) {
    return statusor.status();
  }

  return Status::OK;
}

// Creates and returns factories for all supported ResourceHandlers.
StatusOr<vector<ResourceHandlerFactory *>> CreateSupportedResources(
    CgroupFactory *cgroup_factory, const KernelApi *kernel,
    EventFdNotifications *eventfd_notifications) {
  // Create the available resource handlers.
  vector<ResourceHandlerFactory *> resource_factories;
  RETURN_IF_ERROR(
      AppendIfAvailable(CpuResourceHandlerFactory::New(cgroup_factory, kernel,
                                                       eventfd_notifications),
                        &resource_factories));
  RETURN_IF_ERROR(
      AppendIfAvailable(MemoryResourceHandlerFactory::New(
                            cgroup_factory, kernel, eventfd_notifications),
                        &resource_factories));
  RETURN_IF_ERROR(
      AppendIfAvailable(DeviceResourceHandlerFactory::New(
                            cgroup_factory, kernel, eventfd_notifications),
                        &resource_factories));
  RETURN_IF_ERROR(
      AppendIfAvailable(MonitoringResourceHandlerFactory::New(
                            cgroup_factory, kernel, eventfd_notifications),
                        &resource_factories));

  return resource_factories;
}

}  // namespace lmctfy
}  // namespace containers
