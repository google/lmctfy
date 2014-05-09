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

#ifndef SRC_CONTROLLERS_RLIMIT_CONTROLLER_MOCK_H_
#define SRC_CONTROLLERS_RLIMIT_CONTROLLER_MOCK_H_

#include "lmctfy/controllers/rlimit_controller.h"

#include "gmock/gmock.h"

namespace containers {
namespace lmctfy {

class MockRLimitControllerFactory : public RLimitControllerFactory {
 public:
  // The mock won't use the additional parameters so it is ok to fake them.
  explicit MockRLimitControllerFactory(const CgroupFactory *cgroup_factory)
      : RLimitControllerFactory(
            cgroup_factory, reinterpret_cast<KernelApi *>(0xFFFFFFFF),
            reinterpret_cast<EventFdNotifications *>(0xFFFFFFFF)) {}

  MOCK_CONST_METHOD1(
      Get, ::util::StatusOr<RLimitController *>(const string &hierarchy_path));
  MOCK_CONST_METHOD1(Create, ::util::StatusOr<RLimitController *>(
                                 const string &hierarchy_path));
};

typedef ::testing::StrictMock<MockRLimitControllerFactory>
    StrictMockRLimitControllerFactory;
typedef ::testing::NiceMock<MockRLimitControllerFactory>
    NiceMockRLimitControllerFactory;

class MockRLimitController : public RLimitController {
 public:
  // The mock won't use the additional parameters so it is okay to fake them.
  MockRLimitController()
      : RLimitController(
          "", "", false, reinterpret_cast<KernelApi *>(0xFFFFFFFF),
          reinterpret_cast<EventFdNotifications *>(0xFFFFFFFF)) {
  }

  MOCK_METHOD1(SetFdLimit, ::util::Status(int64 limit));
  MOCK_CONST_METHOD0(GetFdLimit, ::util::StatusOr<int64>());
  MOCK_CONST_METHOD0(GetFdUsage, ::util::StatusOr<int64>());
  MOCK_CONST_METHOD0(GetMaxFdUsage, ::util::StatusOr<int64>());
  MOCK_CONST_METHOD0(GetFdFailCount, ::util::StatusOr<int64>());
};

typedef ::testing::StrictMock<MockRLimitController> StrictMockRLimitController;
typedef ::testing::NiceMock<MockRLimitController> NiceMockRLimitController;

}  // namespace lmctfy
}  // namespace containers

#endif  // SRC_CONTROLLERS_RLIMIT_CONTROLLER_MOCK_H_
