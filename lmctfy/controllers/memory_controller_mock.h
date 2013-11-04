// Copyright 2013 Google Inc. All Rights Reserved.
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

#ifndef SRC_CONTROLLERS_MEMORY_CONTROLLER_MOCK_H_
#define SRC_CONTROLLERS_MEMORY_CONTROLLER_MOCK_H_

#include "lmctfy/controllers/memory_controller.h"

#include "gmock/gmock.h"

namespace containers {
namespace lmctfy {

class MockMemoryControllerFactory : public MemoryControllerFactory {
 public:
  // The mock won't use the additional parameters so it is okay to fake them.
  explicit MockMemoryControllerFactory(const CgroupFactory *cgroup_factory)
      : MemoryControllerFactory(
            cgroup_factory, reinterpret_cast<KernelApi *>(0xFFFFFFFF),
            reinterpret_cast<EventFdNotifications *>(0xFFFFFFFF)) {}

  MOCK_CONST_METHOD1(
      Get, ::util::StatusOr<MemoryController *>(const string &hierarchy_path));
  MOCK_CONST_METHOD1(Create, ::util::StatusOr<MemoryController *>(
                                 const string &hierarchy_path));
};

typedef ::testing::StrictMock<MockMemoryControllerFactory>
    StrictMockMemoryControllerFactory;
typedef ::testing::NiceMock<MockMemoryControllerFactory>
    NiceMockMemoryControllerFactory;

class MockMemoryController : public MemoryController {
 public:
  // The mock won't use the additional parameters so it is okay to fake them.
  MockMemoryController()
      : MemoryController("", false, reinterpret_cast<KernelApi *>(0xFFFFFFFF),
                         reinterpret_cast<EventFdNotifications *>(0xFFFFFFFF)) {
  }

  MOCK_METHOD1(SetLimit, ::util::Status(::util::Bytes limit));
  MOCK_METHOD1(SetSoftLimit, ::util::Status(::util::Bytes limit));

  MOCK_CONST_METHOD0(GetWorkingSet, ::util::StatusOr< ::util::Bytes>());
  MOCK_CONST_METHOD0(GetUsage, ::util::StatusOr< ::util::Bytes>());
  MOCK_CONST_METHOD0(GetMaxUsage, ::util::StatusOr< ::util::Bytes>());
  MOCK_CONST_METHOD0(GetLimit, ::util::StatusOr< ::util::Bytes>());
  MOCK_CONST_METHOD0(GetEffectiveLimit, ::util::StatusOr< ::util::Bytes>());
  MOCK_CONST_METHOD0(GetSoftLimit, ::util::StatusOr< ::util::Bytes>());
  MOCK_METHOD2(RegisterUsageThresholdNotification,
               ::util::StatusOr<ActiveNotifications::Handle>(
                   ::util::Bytes usage_threshold,
                   CgroupController::EventCallback *callback));
  MOCK_METHOD1(RegisterOomNotification,
               ::util::StatusOr<ActiveNotifications::Handle>(
                   CgroupController::EventCallback *callback));
};

typedef ::testing::StrictMock<MockMemoryController> StrictMockMemoryController;
typedef ::testing::NiceMock<MockMemoryController> NiceMockMemoryController;

}  // namespace lmctfy
}  // namespace containers

#endif  // SRC_CONTROLLERS_MEMORY_CONTROLLER_MOCK_H_
