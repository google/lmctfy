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

#ifndef SRC_CONTROLLERS_CPUSET_CONTROLLER_MOCK_H_
#define SRC_CONTROLLERS_CPUSET_CONTROLLER_MOCK_H_

#include "lmctfy/controllers/cpuset_controller.h"

#include "util/cpu_mask.h"
#include "gmock/gmock.h"

namespace containers {
namespace lmctfy {

class MockCpusetControllerFactory : public CpusetControllerFactory {
 public:
  // The mock won't use the additional parameters so it is okay to fake them.
  explicit MockCpusetControllerFactory(const CgroupFactory *cgroup_factory)
      : CpusetControllerFactory(
            cgroup_factory, reinterpret_cast<KernelApi *>(0xFFFFFFFF),
            reinterpret_cast<EventFdNotifications *>(0xFFFFFFFF)) {}

  MOCK_CONST_METHOD1(
      Get, ::util::StatusOr<CpusetController *>(const string &hierarchy_path));
  MOCK_CONST_METHOD1(Create, ::util::StatusOr<CpusetController *>(
                                 const string &hierarchy_path));
};

typedef ::testing::StrictMock<MockCpusetControllerFactory>
    StrictMockCpusetControllerFactory;
typedef ::testing::NiceMock<MockCpusetControllerFactory>
    NiceMockCpusetControllerFactory;

class MockCpusetController : public CpusetController {
 public:
  // The mock won't use the additional parameters so it is okay to fake them.
  MockCpusetController()
      : CpusetController(
          "", "", false, reinterpret_cast<KernelApi *>(0xFFFFFFFF),
          reinterpret_cast<EventFdNotifications *>(0xFFFFFFFF)) {
  }

  MOCK_METHOD1(SetCpuMask, ::util::Status(
      const ::util::CpuMask &mask));
  MOCK_METHOD1(SetMemoryNodes,
               ::util::Status(const util::ResSet &memory_nodes));
  MOCK_CONST_METHOD0(GetCpuMask,
                     ::util::StatusOr<::util::CpuMask>());
  MOCK_CONST_METHOD0(GetMemoryNodes, ::util::StatusOr<util::ResSet>());
  MOCK_METHOD0(EnableCloneChildren, ::util::Status());
  MOCK_METHOD0(DisableCloneChildren, ::util::Status());
};

typedef ::testing::StrictMock<MockCpusetController> StrictMockCpusetController;
typedef ::testing::NiceMock<MockCpusetController> NiceMockCpusetController;

}  // namespace lmctfy
}  // namespace containers

#endif  // SRC_CONTROLLERS_CPUSET_CONTROLLER_MOCK_H_
