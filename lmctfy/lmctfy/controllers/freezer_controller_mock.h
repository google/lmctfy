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

#ifndef SRC_CONTROLLERS_FREEZER_CONTROLLER_MOCK_H_
#define SRC_CONTROLLERS_FREEZER_CONTROLLER_MOCK_H_

#include "lmctfy/controllers/freezer_controller.h"

#include "gmock/gmock.h"

namespace containers {
namespace lmctfy {

class MockFreezerControllerFactory : public FreezerControllerFactory {
 public:
  // The mock won't use the additional parameters so it is okay to fake them.
  explicit MockFreezerControllerFactory(const CgroupFactory *cgroup_factory)
      : FreezerControllerFactory(
            cgroup_factory, reinterpret_cast<KernelApi *>(0xFFFFFFFF),
            reinterpret_cast<EventFdNotifications *>(0xFFFFFFFF), true) {}

  MOCK_CONST_METHOD1(
      Get, ::util::StatusOr<FreezerController *>(const string &hierarchy_path));
  MOCK_CONST_METHOD1(Create, ::util::StatusOr<FreezerController *>(
                                 const string &hierarchy_path));
  MOCK_CONST_METHOD1(Exists, bool(const string &hierarchy_path));
};

typedef ::testing::StrictMock<MockFreezerControllerFactory>
    StrictMockFreezerControllerFactory;
typedef ::testing::NiceMock<MockFreezerControllerFactory>
    NiceMockFreezerControllerFactory;

class MockFreezerController : public FreezerController {
 public:
  // The mock won't use the additional parameters so it is okay to fake them.
  MockFreezerController()
      : FreezerController(
            "", "", false, reinterpret_cast<KernelApi *>(0xFFFFFFFF),
            reinterpret_cast<EventFdNotifications *>(0xFFFFFFFF)) {}

  MOCK_METHOD0(Destroy, ::util::Status());
  MOCK_METHOD1(Enter, ::util::Status(pid_t tid));
  MOCK_METHOD2(Delegate, ::util::Status(::util::UnixUid uid,
                                        ::util::UnixGid gid));
  MOCK_CONST_METHOD1(PopulateMachineSpec, ::util::Status(MachineSpec *spec));
  MOCK_CONST_METHOD0(GetThreads, ::util::StatusOr< ::std::vector<pid_t>>());
  MOCK_CONST_METHOD0(GetProcesses, ::util::StatusOr< ::std::vector<pid_t>>());
  MOCK_CONST_METHOD0(GetSubcontainers,
                     ::util::StatusOr< ::std::vector<string>>());

  MOCK_METHOD0(Freeze, ::util::Status());
  MOCK_METHOD0(Unfreeze, ::util::Status());
  MOCK_CONST_METHOD0(State, ::util::StatusOr<FreezerState>());
};

typedef ::testing::StrictMock<MockFreezerController>
    StrictMockFreezerController;
typedef ::testing::NiceMock<MockFreezerController> NiceMockFreezerController;

}  // namespace lmctfy
}  // namespace containers

#endif  // SRC_CONTROLLERS_FREEZER_CONTROLLER_MOCK_H_
