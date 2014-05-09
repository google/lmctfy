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

#ifndef SRC_CONTROLLERS_DEVICE_CONTROLLER_H_
#define SRC_CONTROLLERS_DEVICE_CONTROLLER_H_

#include <string>
using ::std::string;
#include <vector>

#include "base/macros.h"
#include "lmctfy/controllers/cgroup_controller.h"
#include "util/task/status.h"

namespace containers {
namespace lmctfy {

class DeviceController;

// Factory for DeviceControllers.
//
// Class is thread-safe.
class DeviceControllerFactory
    : public CgroupControllerFactory<DeviceController, CGROUP_DEVICE> {
 public:
  // Does not own cgroup_factory or kernel.
  DeviceControllerFactory(const CgroupFactory *cgroup_factory,
                          const KernelApi *kernel,
                          EventFdNotifications *eventfd_notifications)
      : CgroupControllerFactory<DeviceController, CGROUP_DEVICE>(
            cgroup_factory, kernel, eventfd_notifications) {}

  ~DeviceControllerFactory() override {}

 private:
  DISALLOW_COPY_AND_ASSIGN(DeviceControllerFactory);
};


// Controller for device cgroups.
//
// Class is thread-safe.
class DeviceController : public CgroupController {
 public:
  // Does not take ownership of kernel.
  DeviceController(const string &hierarchy_path, const string &cgroup_path,
                   bool owns_cgroup, const KernelApi *kernel,
                   EventFdNotifications *eventfd_notifications)
      : CgroupController(CGROUP_DEVICE, hierarchy_path, cgroup_path,
                         owns_cgroup, kernel, eventfd_notifications) {}
  ~DeviceController() override {}

  // Set device access restrictions on the cgroup.
  virtual ::util::Status SetRestrictions(
      const DeviceSpec::DeviceRestrictionsSet &restrictions);

  // Returns the current state of the cgroup.
  virtual ::util::StatusOr<DeviceSpec::DeviceRestrictionsSet> GetState() const;

  // Verify that a device restriction rule is valid.
  virtual ::util::Status VerifyRestriction(
      const DeviceSpec::DeviceRestrictions &rule) const;

 private:
  // Helper method to parse and set the type for device from a rule string.
  ::util::Status SetDeviceType(
      const string &rule, DeviceSpec::DeviceRestrictions *restriction) const;

  // Helper method to parse and set major and minor device numbers from a
  // rule string.
  ::util::Status SetDeviceNumber(
      const string &rule, DeviceSpec::DeviceRestrictions *restriction) const;

  // Helper method to parse and set device access types (read, write, mknod)
  // from a rule string.
  ::util::Status SetDeviceAccess(
      const string &rule, DeviceSpec::DeviceRestrictions *restriction) const;

  // Return a restriction rule that denies access to all devices.
  DeviceSpec::DeviceRestrictionsSet GetAllDevicesDeniedRule() const;

  DISALLOW_COPY_AND_ASSIGN(DeviceController);
};

}  // namespace lmctfy
}  // namespace containers

#endif  // SRC_CONTROLLERS_DEVICE_CONTROLLER_H_
