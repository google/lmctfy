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

#ifndef SRC_CONTROLLERS_CPUACCT_CONTROLLER_MOCK_H_
#define SRC_CONTROLLERS_CPUACCT_CONTROLLER_MOCK_H_

#include "lmctfy/controllers/cpuacct_controller.h"

#include "gmock/gmock.h"

namespace containers {
namespace lmctfy {

class MockCpuAcctControllerFactory : public CpuAcctControllerFactory {
 public:
  explicit MockCpuAcctControllerFactory(const CgroupFactory *cgroup_factory)
      : CpuAcctControllerFactory(
            cgroup_factory, reinterpret_cast<KernelApi *>(0xFFFFFFFF),
            reinterpret_cast<EventFdNotifications *>(0xFFFFFFFF)) {}

  MOCK_CONST_METHOD1(
      Get, ::util::StatusOr<CpuAcctController *>(const string &hierarchy_path));
  MOCK_CONST_METHOD1(Create, ::util::StatusOr<CpuAcctController *>(
                                 const string &hierarchy_path));
};

typedef ::testing::StrictMock<MockCpuAcctControllerFactory>
    StrictMockCpuAcctControllerFactory;
typedef ::testing::NiceMock<MockCpuAcctControllerFactory>
    NiceMockCpuAcctControllerFactory;

class MockCpuAcctController : public CpuAcctController {
 public:
  MockCpuAcctController()
      : CpuAcctController(
            "", "", false, reinterpret_cast<KernelApi *>(0xFFFFFFFF),
            reinterpret_cast<EventFdNotifications *>(0xFFFFFFFF)) {}

  MOCK_CONST_METHOD0(GetCpuUsageInNs, ::util::StatusOr<int64>());
  MOCK_CONST_METHOD0(GetCpuTime, ::util::StatusOr<CpuTime>());
  MOCK_CONST_METHOD0(GetPerCpuUsageInNs,
                     ::util::StatusOr< ::std::vector<int64> *>());
  MOCK_METHOD0(SetupHistograms, ::util::Status());
  MOCK_CONST_METHOD0(GetSchedulerHistograms,
                     ::util::StatusOr< ::std::vector<CpuHistogramData *> *>());
  MOCK_CONST_METHOD0(EnableSchedulerHistograms, ::util::Status());
};

typedef ::testing::StrictMock<MockCpuAcctController>
    StrictMockCpuAcctController;
typedef ::testing::NiceMock<MockCpuAcctController> NiceMockCpuAcctController;

}  // namespace lmctfy
}  // namespace containers

#endif  // SRC_CONTROLLERS_CPUACCT_CONTROLLER_MOCK_H_
