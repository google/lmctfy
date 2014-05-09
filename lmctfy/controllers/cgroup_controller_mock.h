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

#ifndef SRC_CONTROLLERS_CGROUP_CONTROLLER_MOCK_H_
#define SRC_CONTROLLERS_CGROUP_CONTROLLER_MOCK_H_

#include "lmctfy/controllers/cgroup_controller.h"

#include "gmock/gmock.h"

namespace containers {
namespace lmctfy {

class MockCgroupController : public CgroupController {
 public:
  // It is okay to use a fake kernel and eventfd_notifications since those are
  // unused by the mock.
  MockCgroupController(CgroupHierarchy type, const string &cgroup_path,
                       bool owns_cgroup)
      : CgroupController(type, "", cgroup_path, owns_cgroup,
                         reinterpret_cast<KernelApi *>(0xFFFFFFFF),
                         reinterpret_cast<EventFdNotifications *>(0xFFFFFFFF)) {
  }

  MOCK_METHOD0(Destroy, ::util::Status());
  MOCK_METHOD1(Enter, ::util::Status(pid_t tid));
  MOCK_METHOD2(Delegate, ::util::Status(::util::UnixUid uid,
                                        ::util::UnixGid gid));
  MOCK_METHOD1(SetChildrenLimit, ::util::Status(int64 limit));
  MOCK_CONST_METHOD0(GetThreads, ::util::StatusOr< ::std::vector<pid_t>>());
  MOCK_CONST_METHOD0(GetProcesses, ::util::StatusOr< ::std::vector<pid_t>>());
  MOCK_CONST_METHOD0(GetSubcontainers,
                     ::util::StatusOr< ::std::vector<string>>());
  MOCK_CONST_METHOD0(GetChildrenLimit, ::util::StatusOr<int64>());
  MOCK_METHOD0(EnableCloneChildren, ::util::Status());
  MOCK_METHOD0(DisableCloneChildren, ::util::Status());
  MOCK_CONST_METHOD1(PopulateMachineSpec, ::util::Status(MachineSpec *spec));
};

typedef ::testing::StrictMock<MockCgroupController> StrictMockCgroupController;
typedef ::testing::NiceMock<MockCgroupController> NiceMockCgroupController;

}  // namespace lmctfy
}  // namespace containers

#endif  // SRC_CONTROLLERS_CGROUP_CONTROLLER_MOCK_H_
