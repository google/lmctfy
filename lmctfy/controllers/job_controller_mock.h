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

#ifndef SRC_CONTROLLERS_JOB_CONTROLLER_MOCK_H_
#define SRC_CONTROLLERS_JOB_CONTROLLER_MOCK_H_

#include "lmctfy/controllers/job_controller.h"

#include "gmock/gmock.h"

namespace containers {
namespace lmctfy {

class MockJobControllerFactory : public JobControllerFactory {
 public:
  // The mock won't use the additional parameters so it is okay to fake them.
  explicit MockJobControllerFactory(const CgroupFactory *cgroup_factory)
      : JobControllerFactory(
            cgroup_factory, reinterpret_cast<KernelApi *>(0xFFFFFFFF),
            reinterpret_cast<EventFdNotifications *>(0xFFFFFFFF)) {}

  MOCK_CONST_METHOD1(
      Get, ::util::StatusOr<JobController *>(const string &hierarchy_path));
  MOCK_CONST_METHOD1(
      Create, ::util::StatusOr<JobController *>(const string &hierarchy_path));
  MOCK_CONST_METHOD1(Exists, bool(const string &hierarchy_path));
  MOCK_CONST_METHOD1(DetectCgroupPath, ::util::StatusOr<string>(pid_t tid));
  MOCK_CONST_METHOD0(HierarchyName, string());
};

typedef ::testing::StrictMock<MockJobControllerFactory>
    StrictMockJobControllerFactory;
typedef ::testing::NiceMock<MockJobControllerFactory>
    NiceMockJobControllerFactory;

class MockJobController : public JobController {
 public:
  // The mock won't use the additional parameters so it is okay to fake them.
  MockJobController()
      : JobController("", "", false, reinterpret_cast<KernelApi *>(0xFFFFFFFF),
                      reinterpret_cast<EventFdNotifications *>(0xFFFFFFFF)) {}

  MOCK_METHOD0(Destroy, ::util::Status());
  MOCK_METHOD1(Enter, ::util::Status(pid_t tid));

  MOCK_CONST_METHOD0(GetThreads, ::util::StatusOr< ::std::vector<pid_t>>());
  MOCK_CONST_METHOD0(GetProcesses, ::util::StatusOr< ::std::vector<pid_t>>());
  MOCK_CONST_METHOD0(GetSubcontainers,
                     ::util::StatusOr< ::std::vector<string>>());
};

typedef ::testing::StrictMock<MockJobController> StrictMockJobController;
typedef ::testing::NiceMock<MockJobController> NiceMockJobController;

}  // namespace lmctfy
}  // namespace containers

#endif  // SRC_CONTROLLERS_JOB_CONTROLLER_MOCK_H_
