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

#ifndef SRC_CONTROLLERS_CGROUP_FACTORY_MOCK_H_
#define SRC_CONTROLLERS_CGROUP_FACTORY_MOCK_H_

#include "lmctfy/controllers/cgroup_factory.h"

#include "base/casts.h"
#include "base/macros.h"
#include "gmock/gmock.h"

namespace containers {
namespace lmctfy {

class MockCgroupFactory : public CgroupFactory {
 public:
  // We set the kernel pointer to a bogus value since it is unused by this mock.
  MockCgroupFactory()
      : CgroupFactory({{CGROUP_CPU, "/dev/cgroup/cpu"},
                       {CGROUP_CPUACCT, "/dev/cgroup/cpu"},
                       {CGROUP_MEMORY, "/dev/cgroup/memory"},
                       {CGROUP_NET, "/dev/cgroup/net"}},
                      reinterpret_cast<KernelApi *>(0xFFFFFFFF)) {}

  MOCK_CONST_METHOD2(Get,
                     ::util::StatusOr<string>(CgroupHierarchy type,
                                              const string &hierarchy_path));
  MOCK_CONST_METHOD2(Create,
                     ::util::StatusOr<string>(CgroupHierarchy type,
                                              const string &hierarchy_path));
  MOCK_CONST_METHOD1(OwnsCgroup, bool(CgroupHierarchy type));
  MOCK_METHOD1(Mount, ::util::Status(const CgroupMount &cgroup));
  MOCK_CONST_METHOD1(IsMounted, bool(CgroupHierarchy type));
  MOCK_CONST_METHOD2(DetectCgroupPath,
                     ::util::StatusOr<string>(pid_t tid,
                                              CgroupHierarchy hierarchy));
  MOCK_CONST_METHOD1(GetHierarchyName, string(CgroupHierarchy hierarchy));
  MOCK_CONST_METHOD0(GetSupportedHierarchies, ::std::vector<CgroupHierarchy>());
  MOCK_CONST_METHOD1(PopulateMachineSpec, ::util::Status(MachineSpec *spec));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockCgroupFactory);
};

typedef ::testing::StrictMock<MockCgroupFactory> StrictMockCgroupFactory;
typedef ::testing::NiceMock<MockCgroupFactory> NiceMockCgroupFactory;

}  // namespace lmctfy
}  // namespace containers

#endif  // SRC_CONTROLLERS_CGROUP_FACTORY_MOCK_H_
