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

#ifndef SRC_RESOURCES_DEVICE_RESOURCE_HANDLER_H_
#define SRC_RESOURCES_DEVICE_RESOURCE_HANDLER_H_

#include <memory>
#include <string>
using ::std::string;

#include "base/macros.h"
#include "system_api/kernel_api.h"
#include "lmctfy/controllers/device_controller.h"
#include "lmctfy/resources/cgroup_resource_handler.h"
#include "include/lmctfy.h"
#include "util/errors.h"
#include "util/task/statusor.h"

namespace containers {
namespace lmctfy {

class ContainerSpec;
class ContainerStats;
class EventFdNotifications;

typedef ::system_api::KernelAPI KernelApi;

class DeviceResourceHandlerFactory : public CgroupResourceHandlerFactory {
 public:
  // Create an instance of this factory. If the resource is not supported on
  // this machine a NOT_FOUND error is returned. Does not take ownership of
  // any argument.
  static ::util::StatusOr<DeviceResourceHandlerFactory *> New(
      CgroupFactory *cgroup_factory, const KernelApi *kernel,
      EventFdNotifications *eventfd_notifications);

  // Takes ownership of device_controller_factory. Does not own
  // cgroup_factory or kernel.
  DeviceResourceHandlerFactory(
      const DeviceControllerFactory *device_controller_factory,
      CgroupFactory *cgroup_factory,
      const KernelApi *kernel);
  ~DeviceResourceHandlerFactory() override {}

 protected:
  virtual ::util::StatusOr<ResourceHandler *> GetResourceHandler(
      const string &container_name) const override;
  virtual ::util::StatusOr<ResourceHandler *> CreateResourceHandler(
      const string &container_name, const ContainerSpec &spec) const override;

 private:
  // Controller factory for device cgroup controllers.
  const ::std::unique_ptr<const DeviceControllerFactory>
      device_controller_factory_;

  friend class DeviceResourceHandlerFactoryTest;
  DISALLOW_COPY_AND_ASSIGN(DeviceResourceHandlerFactory);
};

class DeviceResourceHandler : public CgroupResourceHandler {
 public:
  // Does not own kernel. Takes ownership of device_controller.
  DeviceResourceHandler(
      const string &container_name,
      const KernelApi *kernel,
      DeviceController *device_controller);
  ~DeviceResourceHandler() override {}

  // Update a container config.
  ::util::Status DoUpdate(const ContainerSpec &spec) override;
  // Get Stats for the existing container.
  ::util::Status Stats(Container::StatsType type,
                       ContainerStats *output) const override;
  // Get Spec for the existing container.
  ::util::Status Spec(ContainerSpec *spec) const override;
  // Fill in any missing fields in the spec with defaults, if applicable.
  void RecursiveFillDefaults(ContainerSpec *spec) const override;
  // Verify that a given spec is valid.
  ::util::Status VerifyFullSpec(const ContainerSpec &spec) const override;
  // Register for events of interest.
  ::util::StatusOr<Container::NotificationId> RegisterNotification(
      const EventSpec &spec, Callback1< ::util::Status> *callback) override;

 private:
  DeviceController *device_controller_;

  DISALLOW_COPY_AND_ASSIGN(DeviceResourceHandler);
};

}  // namespace lmctfy
}  // namespace containers


#endif  // SRC_RESOURCES_DEVICE_RESOURCE_HANDLER_H_
