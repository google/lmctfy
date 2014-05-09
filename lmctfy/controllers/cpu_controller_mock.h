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

#ifndef SRC_CONTROLLERS_CPU_CONTROLLER_MOCK_H_
#define SRC_CONTROLLERS_CPU_CONTROLLER_MOCK_H_

#include "lmctfy/controllers/cpu_controller.h"

#include "gmock/gmock.h"

namespace containers {
namespace lmctfy {

class MockCpuControllerFactory : public CpuControllerFactory {
 public:
  // The mock won't use the additional parameters so it is okay to fake them.
  explicit MockCpuControllerFactory(const CgroupFactory *cgroup_factory)
      : CpuControllerFactory(
            cgroup_factory, reinterpret_cast<KernelApi *>(0xFFFFFFFF),
            reinterpret_cast<EventFdNotifications *>(0xFFFFFFFF)) {}

  MOCK_CONST_METHOD1(
      Get, ::util::StatusOr<CpuController *>(const string &hierarchy_path));
  MOCK_CONST_METHOD1(
      Create, ::util::StatusOr<CpuController *>(const string &hierarchy_path));
  MOCK_CONST_METHOD1(Exists, bool(const string &hierarchy_path));
};

typedef ::testing::StrictMock<MockCpuControllerFactory>
    StrictMockCpuControllerFactory;
typedef ::testing::NiceMock<MockCpuControllerFactory>
    NiceMockCpuControllerFactory;

class MockCpuController : public CpuController {
 public:
  // The mock won't use the additional parameters so it is okay to fake them.
  MockCpuController()
      : CpuController("", "", false, reinterpret_cast<KernelApi *>(0xFFFFFFFF),
                      reinterpret_cast<EventFdNotifications *>(0xFFFFFFFF)) {}

  MOCK_METHOD1(SetMilliCpus, ::util::Status(int64 milli_cpus));
  MOCK_METHOD1(SetMaxMilliCpus, ::util::Status(int64 max_milli_cpus));
  MOCK_METHOD1(SetLatency, ::util::Status(SchedulingLatency latency));
  MOCK_METHOD1(SetPlacementStrategy, ::util::Status(int64 placement_strategy));
  MOCK_CONST_METHOD0(GetNumRunnable, ::util::StatusOr<int>());
  MOCK_CONST_METHOD0(GetMilliCpus, ::util::StatusOr<int64>());
  MOCK_CONST_METHOD0(GetMaxMilliCpus, ::util::StatusOr<int64>());
  MOCK_CONST_METHOD0(GetLatency, ::util::StatusOr<SchedulingLatency>());
  MOCK_CONST_METHOD0(GetPlacementStrategy, ::util::StatusOr<int64>());
  MOCK_CONST_METHOD0(GetThrottlingStats, ::util::StatusOr<ThrottlingStats>());
  MOCK_CONST_METHOD0(GetThrottlingPeriodInMs, ::util::StatusOr<int64>());
};

typedef ::testing::StrictMock<MockCpuController> StrictMockCpuController;
typedef ::testing::NiceMock<MockCpuController> NiceMockCpuController;

}  // namespace lmctfy
}  // namespace containers

#endif  // SRC_CONTROLLERS_CPU_CONTROLLER_MOCK_H_
