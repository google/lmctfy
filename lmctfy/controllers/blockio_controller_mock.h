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

#ifndef SRC_CONTROLLERS_BLOCKIO_CONTROLLER_MOCK_H_
#define SRC_CONTROLLERS_BLOCKIO_CONTROLLER_MOCK_H_

#include "lmctfy/controllers/blockio_controller.h"

#include "gmock/gmock.h"

namespace containers {
namespace lmctfy {

class MockBlockIoControllerFactory : public BlockIoControllerFactory {
 public:
  // The mock won't use the additional parameters so it is okay to fake them.
  explicit MockBlockIoControllerFactory(const CgroupFactory *cgroup_factory)
      : BlockIoControllerFactory(
            cgroup_factory, reinterpret_cast<KernelApi *>(0xFFFFFFFF),
            reinterpret_cast<EventFdNotifications *>(0xFFFFFFFF)) {}

  MOCK_CONST_METHOD1(
      Get, ::util::StatusOr<BlockIoController *>(const string &hierarchy_path));
  MOCK_CONST_METHOD1(Create, ::util::StatusOr<BlockIoController *>(
                                 const string &hierarchy_path));
  MOCK_CONST_METHOD1(Exists, bool(const string &hierarchy_path));
};

typedef ::testing::StrictMock<MockBlockIoControllerFactory>
    StrictMockBlockIoControllerFactory;
typedef ::testing::NiceMock<MockBlockIoControllerFactory>
    NiceMockBlockIoControllerFactory;

class MockBlockIoController : public BlockIoController {
 public:
  // The mock won't use the additional parameters so it is okay to fake them.
  MockBlockIoController()
      : BlockIoController(
          "", "", false, reinterpret_cast<KernelApi *>(0xFFFFFFFF),
          reinterpret_cast<EventFdNotifications *>(0xFFFFFFFF)) {}

  MOCK_METHOD1(UpdateDefaultLimit, ::util::Status(uint32 limit));
  MOCK_CONST_METHOD0(GetDefaultLimit, ::util::Status<uint32>());
  MOCK_METHOD1(UpdatePerDeviceLimit, ::util::Status(
      const BlockIoSpec::DeviceLimitSet &device_limits));
  MOCK_CONST_METHOD0(GetDeviceLimits,
                     ::util::StatusOr<BlockIoSpec::DeviceLimitSet>());
  MOCK_METHOD1(UpdateMaxLimit, ::util::Status(
      const BlockIoSpec::MaxLimitSet &max_limits));
  MOCK_CONST_METHOD0(GetMaxLimit, ::util::StatusOr<BlockIoSpec::MaxLimitSet>());
};

typedef ::testing::StrictMock<MockBlockIoController>
      StrictMockBlockIoController;
typedef ::testing::NiceMock<MockBlockIoController> NiceMockBlockIoController;

}  // namespace lmctfy
}  // namespace containers

#endif  // SRC_CONTROLLERS_BLOCKIO_CONTROLLER_MOCK_H_
