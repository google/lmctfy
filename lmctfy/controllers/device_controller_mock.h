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

#ifndef SRC_CONTROLLERS_DEVICE_CONTROLLER_MOCK_H_
#define SRC_CONTROLLERS_DEVICE_CONTROLLER_MOCK_H_

#include "lmctfy/controllers/device_controller.h"

#include "gmock/gmock.h"

namespace containers {
namespace lmctfy {

class MockDeviceControllerFactory : public DeviceControllerFactory {
 public:
  explicit MockDeviceControllerFactory(const CgroupFactory *cgroup_factory)
      : DeviceControllerFactory(
          cgroup_factory, reinterpret_cast<KernelApi *>(0xFFFFFFFF),
          reinterpret_cast<EventFdNotifications *>(0xFFFFFFFF)) {}

  MOCK_CONST_METHOD1(
      Get, ::util::StatusOr<DeviceController *>(const string &hierarchy_path));
  MOCK_CONST_METHOD1(Create, ::util::StatusOr<DeviceController *>(
                                 const string &hierarchy_path));
  MOCK_CONST_METHOD1(Exists, bool(const string &hierarchy_path));
};

typedef ::testing::StrictMock<MockDeviceControllerFactory>
    StrictMockDeviceControllerFactory;
typedef ::testing::NiceMock<MockDeviceControllerFactory>
    NiceMockDeviceControllerFactory;

class MockDeviceController : public DeviceController {
 public:
  // The mock won't use the additional parameters so it is okay to fake them.
  MockDeviceController()
      : DeviceController(
          "", "", false, reinterpret_cast<KernelApi *>(0xFFFFFFFF),
          reinterpret_cast<EventFdNotifications *>(0xFFFFFFFF)) {}

  MOCK_METHOD1(SetRestrictions, ::util::Status(
      const DeviceSpec::DeviceRestrictionsSet &restrictions));
  MOCK_CONST_METHOD0(GetState,
                     ::util::StatusOr<DeviceSpec::DeviceRestrictionsSet>());
  MOCK_CONST_METHOD1(VerifyRestriction, ::util::Status(
      const DeviceSpec::DeviceRestrictions &rule));
};

typedef ::testing::StrictMock<MockDeviceController> StrictMockDeviceController;
typedef ::testing::NiceMock<MockDeviceController> NiceMockDeviceController;

}  // namespace lmctfy
}  // namespace containers
#endif  // SRC_CONTROLLERS_DEVICE_CONTROLLER_MOCK_H_
