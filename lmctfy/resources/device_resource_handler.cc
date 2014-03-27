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

#include "lmctfy/resources/device_resource_handler.h"

#include "file/base/path.h"
#include "util/errors.h"
#include "util/task/codes.pb.h"

using ::file::Basename;
using ::file::JoinPath;
using ::std::vector;
using ::util::Status;
using ::util::StatusOr;
using ::util::error::INVALID_ARGUMENT;

namespace containers {
namespace lmctfy {

StatusOr<DeviceResourceHandlerFactory *>
DeviceResourceHandlerFactory::New(
    CgroupFactory *cgroup_factory, const KernelApi *kernel,
    EventFdNotifications *eventfd_notifications) {
  // Device hierarchy must be mounted.
  if (!cgroup_factory->IsMounted(DeviceControllerFactory::HierarchyType())) {
    return Status(::util::error::NOT_FOUND,
                  "Device resource depends on the device cgroup hierarchy");
  }

  // Create device controller.
  DeviceControllerFactory *device_controller = new DeviceControllerFactory(
      cgroup_factory, kernel, eventfd_notifications);

  return new DeviceResourceHandlerFactory(device_controller, cgroup_factory,
                                              kernel);
}

DeviceResourceHandlerFactory::DeviceResourceHandlerFactory(
    const DeviceControllerFactory *device_controller_factory,
    CgroupFactory *cgroup_factory, const KernelApi *kernel)
  : CgroupResourceHandlerFactory(RESOURCE_DEVICE, cgroup_factory, kernel),
  device_controller_factory_(device_controller_factory) {}

StatusOr<ResourceHandler *>
DeviceResourceHandlerFactory::GetResourceHandler(
    const string &container_name) const {
  DeviceController *controller = RETURN_IF_ERROR(
      device_controller_factory_->Get(container_name));
  return new DeviceResourceHandler(container_name, kernel_, controller);
}

StatusOr<ResourceHandler *>
DeviceResourceHandlerFactory::CreateResourceHandler(
    const string &container_name, const ContainerSpec &spec) const {
  DeviceController *controller = RETURN_IF_ERROR(
      device_controller_factory_->Create(container_name));

  return new DeviceResourceHandler(container_name, kernel_, controller);
}

DeviceResourceHandler::DeviceResourceHandler(
    const string &container_name, const KernelApi *kernel,
    DeviceController *device_controller)
  : CgroupResourceHandler(container_name, RESOURCE_DEVICE, kernel,
                          {device_controller}),
  device_controller_(device_controller) {}

Status DeviceResourceHandler::Stats(Container::StatsType type,
                                    ContainerStats *output) const {
  // No Stats to be reported for device restriction rules.
  return Status::OK;
}

Status DeviceResourceHandler::Spec(ContainerSpec *spec) const {
  DeviceSpec *device = spec->mutable_device();
  DeviceSpec::DeviceRestrictionsSet *restriction_set =
      device->mutable_restrictions_set();
  *restriction_set = RETURN_IF_ERROR(device_controller_->GetState());
  return Status::OK;
}

StatusOr<Container::NotificationId>
DeviceResourceHandler::RegisterNotification(
    const EventSpec &spec, Callback1<Status> *callback) {
  ::std::unique_ptr<Callback1<Status>> callback_deleter(callback);
  return Status(::util::error::NOT_FOUND, "No handled event found");
}

Status DeviceResourceHandler::DoUpdate(const ContainerSpec &spec) {
  const DeviceSpec &device = spec.device();
  if (spec.has_device() && device.has_restrictions_set()) {
    RETURN_IF_ERROR(device_controller_->SetRestrictions(
        device.restrictions_set()));
  }
  return Status::OK;
}

void DeviceResourceHandler::RecursiveFillDefaults(
    ContainerSpec *spec) const {
  // There are no default settings. Incomplete spec is rejected by the
  // controller.
}

Status DeviceResourceHandler::VerifyFullSpec(const ContainerSpec &spec) const {
  if (spec.has_device() && spec.device().has_restrictions_set()) {
    for (const auto &rule : spec.device().restrictions_set().restrictions()) {
      RETURN_IF_ERROR(device_controller_->VerifyRestriction(rule));
    }
  }
  return Status::OK;
}

}  // namespace lmctfy
}  // namespace containers
