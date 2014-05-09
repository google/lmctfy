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

#include "lmctfy/controllers/cpuset_controller.h"

#include <memory>

#include "base/integral_types.h"
#include "system_api/kernel_api_mock.h"
#include "file/base/path.h"
#include "lmctfy/controllers/eventfd_notifications_mock.h"
#include "lmctfy/kernel_files.h"
#include "util/cpu_mask.h"
#include "util/cpu_mask_test_util.h"
#include "util/errors_test_util.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::system_api::KernelAPIMock;
using ::util::ResSet;
using ::file::JoinPath;
using ::std::unique_ptr;
using ::testing::NotNull;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::StrictMock;
using ::testing::_;
using ::util::Status;
using ::util::StatusOr;
using ::util::error::NOT_FOUND;

namespace containers {
namespace lmctfy {

using ::util::CpuMask;

static const char kParentMountPoint[] = "/dev/cgroup/cpuset";
static const char kMountPoint[] = "/dev/cgroup/cpuset/test";
static const char kHierarchyPath[] = "/test";

class CpusetControllerTest : public ::testing::Test {
 public:
  virtual void SetUp() {
    mock_kernel_.reset(new StrictMock<KernelAPIMock>());
    mock_eventfd_notifications_.reset(MockEventFdNotifications::NewStrict());
    controller_.reset(new CpusetController(kHierarchyPath, kMountPoint, true,
                                           mock_kernel_.get(),
                                           mock_eventfd_notifications_.get()));
  }

 protected:
  unique_ptr<KernelAPIMock> mock_kernel_;
  unique_ptr<CpusetController> controller_;
  unique_ptr<MockEventFdNotifications> mock_eventfd_notifications_;
};

TEST_F(CpusetControllerTest, Type) {
  EXPECT_EQ(CGROUP_CPUSET, controller_->type());
}

TEST_F(CpusetControllerTest, SetsCpuMask) {
  const string kResFile = JoinPath(kMountPoint, KernelFiles::CPUSet::kCPUs);
  CpuMask cpu_mask(0xF40FF);
  string expected_cpu_string = "0-7,14,16-19";
  EXPECT_CALL(*mock_kernel_, SafeWriteResFile(expected_cpu_string, kResFile,
                                              NotNull(), NotNull()))
      .WillOnce(Return(0));
  EXPECT_OK(controller_->SetCpuMask(cpu_mask));
}

TEST_F(CpusetControllerTest, SetCpuMaskFails) {
  const string kResFile = JoinPath(kMountPoint, KernelFiles::CPUSet::kCPUs);
  CpuMask cpu_mask(0xF40FF);
  string expected_cpu_string = "0-7,14,16-19";
  EXPECT_CALL(*mock_kernel_, SafeWriteResFile(expected_cpu_string, kResFile,
                                              NotNull(), NotNull()))
      .WillOnce(DoAll(SetArgPointee<3>(true), Return(0)));
  EXPECT_NOT_OK(controller_->SetCpuMask(cpu_mask));
}

TEST_F(CpusetControllerTest, GetsCpuMask) {
  const string kResFile = JoinPath(kMountPoint, KernelFiles::CPUSet::kCPUs);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kResFile, NotNull()))
      .WillOnce(DoAll(SetArgPointee<1>("0-4,7,10,12-15"), Return(true)));
  StatusOr<CpuMask> statusor = controller_->GetCpuMask();
  ASSERT_OK(statusor);
  CpuMask cpu_mask = statusor.ValueOrDie();
  EXPECT_EQ(0xF49F, cpu_mask);
}

TEST_F(CpusetControllerTest, GetCpuMaskNotFound) {
  const string kResFile = JoinPath(kMountPoint, KernelFiles::CPUSet::kCPUs);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK))
      .WillRepeatedly(Return(1));
  EXPECT_ERROR_CODE(NOT_FOUND, controller_->GetCpuMask());
}

TEST_F(CpusetControllerTest, GetCpuMaskFails) {
  const string kResFile = JoinPath(kMountPoint, KernelFiles::CPUSet::kCPUs);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kResFile, NotNull()))
      .WillOnce(Return(false));
  EXPECT_NOT_OK(controller_->GetCpuMask());
}

TEST_F(CpusetControllerTest, SetsMemoryNodes) {
  const string kResFile = JoinPath(kMountPoint, KernelFiles::CPUSet::kMemNodes);
  string expected_memory_nodes_string = "0-1";
  EXPECT_CALL(*mock_kernel_, SafeWriteResFile(expected_memory_nodes_string,
                                              kResFile, NotNull(), NotNull()))
      .WillOnce(Return(0));
  ResSet memory_nodes;
  memory_nodes.ReadSetString(expected_memory_nodes_string, ",");
  EXPECT_OK(controller_->SetMemoryNodes(memory_nodes));
}

TEST_F(CpusetControllerTest, SetMemoryNodesFails) {
  const string kResFile = JoinPath(kMountPoint, KernelFiles::CPUSet::kMemNodes);
  string expected_memory_nodes_string = "0-1";
  EXPECT_CALL(*mock_kernel_, SafeWriteResFile(expected_memory_nodes_string,
                                              kResFile, NotNull(), NotNull()))
      .WillOnce(DoAll(SetArgPointee<3>(true), Return(0)));
  ResSet memory_nodes;
  memory_nodes.ReadSetString(expected_memory_nodes_string, ",");
  EXPECT_NOT_OK(controller_->SetMemoryNodes(memory_nodes));
}

TEST_F(CpusetControllerTest, GetsMemoryNodes) {
  const string kResFile = JoinPath(kMountPoint, KernelFiles::CPUSet::kMemNodes);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kResFile, NotNull()))
      .WillOnce(DoAll(SetArgPointee<1>("0-1"), Return(true)));
  StatusOr<ResSet> statusor = controller_->GetMemoryNodes();
  ASSERT_OK(statusor);
  string result_string;
  statusor.ValueOrDie().Format(&result_string);
  EXPECT_EQ("0-1", result_string);
}

TEST_F(CpusetControllerTest, GetMemoryNodesNotFound) {
  const string kResFile = JoinPath(kMountPoint, KernelFiles::CPUSet::kMemNodes);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK))
      .WillRepeatedly(Return(1));
  EXPECT_ERROR_CODE(NOT_FOUND, controller_->GetMemoryNodes());
}

TEST_F(CpusetControllerTest, GetMemoryNodesFails) {
  const string kResFile = JoinPath(kMountPoint, KernelFiles::CPUSet::kMemNodes);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kResFile, NotNull()))
      .WillOnce(Return(false));
  EXPECT_NOT_OK(controller_->GetMemoryNodes());
}

}  // namespace lmctfy
}  // namespace containers
