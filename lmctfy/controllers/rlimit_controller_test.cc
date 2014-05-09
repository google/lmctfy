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

#include "lmctfy/controllers/rlimit_controller.h"

#include <memory>

#include "system_api/kernel_api_mock.h"
#include "file/base/path.h"
#include "lmctfy/controllers/eventfd_notifications_mock.h"
#include "lmctfy/kernel_files.h"
#include "util/errors_test_util.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "util/task/statusor.h"

using ::file::JoinPath;
using ::std::unique_ptr;
using ::testing::DoAll;
using ::testing::NotNull;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::StrictMock;
using ::testing::_;
using ::util::StatusOr;
using ::util::error::NOT_FOUND;

namespace containers {
namespace lmctfy {

static const char kMountPoint[] = "/dev/cgroup/rlimit/test";
static const char kHierarchyPath[] = "/test";

class RLimitControllerTest : public ::testing::Test {
 public:
  virtual void SetUp() {
    mock_kernel_.reset(new StrictMock< ::system_api::KernelAPIMock>());
    mock_eventfd_notifications_.reset(MockEventFdNotifications::NewStrict());
    controller_.reset(new RLimitController(kHierarchyPath, kMountPoint, true,
                                           mock_kernel_.get(),
                                           mock_eventfd_notifications_.get()));
  }

 protected:
  unique_ptr< ::system_api::KernelAPIMock> mock_kernel_;
  unique_ptr<RLimitController> controller_;
  unique_ptr<MockEventFdNotifications> mock_eventfd_notifications_;
};

TEST_F(RLimitControllerTest, Type) {
  EXPECT_EQ(CGROUP_RLIMIT, controller_->type());
}

TEST_F(RLimitControllerTest, SetFdLimitSuccess) {
  const string kResFile = JoinPath(kMountPoint, KernelFiles::RLimit::kFdLimit);

  EXPECT_CALL(*mock_kernel_, SafeWriteResFile("11", kResFile, NotNull(),
                                              NotNull())).WillOnce(Return(0));

  EXPECT_OK(controller_->SetFdLimit(11));
}

TEST_F(RLimitControllerTest, SetFdLimitFails) {
  const string kResFile = JoinPath(kMountPoint, KernelFiles::RLimit::kFdLimit);

  EXPECT_CALL(*mock_kernel_,
              SafeWriteResFile("11", kResFile, NotNull(), NotNull()))
      .WillOnce(DoAll(SetArgPointee<3>(true), Return(1)));

  EXPECT_NOT_OK(controller_->SetFdLimit(11));
}

TEST_F(RLimitControllerTest, GetFdLimitSuccess) {
  const string kResFile = JoinPath(kMountPoint, KernelFiles::RLimit::kFdLimit);

  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK)).WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kResFile, NotNull()))
      .WillOnce(DoAll(SetArgPointee<1>("12"), Return(true)));

  StatusOr<int64> statusor = controller_->GetFdLimit();
  ASSERT_OK(statusor);
  EXPECT_EQ(12, statusor.ValueOrDie());
}

TEST_F(RLimitControllerTest, GetFdLimitNotFound) {
  const string kResFile = JoinPath(kMountPoint, KernelFiles::RLimit::kFdLimit);

  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK)).WillRepeatedly(Return(1));

  EXPECT_ERROR_CODE(NOT_FOUND, controller_->GetFdLimit());
}

TEST_F(RLimitControllerTest, GetFdLimitFails) {
  const string kResFile = JoinPath(kMountPoint, KernelFiles::RLimit::kFdLimit);

  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK)).WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kResFile, NotNull()))
      .WillOnce(Return(false));

  EXPECT_NOT_OK(controller_->GetFdLimit());
}

TEST_F(RLimitControllerTest, GetFdUsageSuccess) {
  const string kResFile = JoinPath(kMountPoint, KernelFiles::RLimit::kFdUsage);

  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK)).WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kResFile, NotNull()))
      .WillOnce(DoAll(SetArgPointee<1>("12"), Return(true)));

  StatusOr<int64> statusor = controller_->GetFdUsage();
  ASSERT_OK(statusor);
  EXPECT_EQ(12, statusor.ValueOrDie());
}

TEST_F(RLimitControllerTest, GetFdUsageNotFound) {
  const string kResFile = JoinPath(kMountPoint, KernelFiles::RLimit::kFdUsage);

  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK)).WillRepeatedly(Return(1));

  EXPECT_ERROR_CODE(NOT_FOUND, controller_->GetFdUsage());
}

TEST_F(RLimitControllerTest, GetFdUsageFails) {
  const string kResFile = JoinPath(kMountPoint, KernelFiles::RLimit::kFdUsage);

  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK)).WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kResFile, NotNull()))
      .WillOnce(Return(false));

  EXPECT_NOT_OK(controller_->GetFdUsage());
}

TEST_F(RLimitControllerTest, GetMaxFdUsageSuccess) {
  const string kResFile =
      JoinPath(kMountPoint, KernelFiles::RLimit::kFdMaxUsage);

  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK)).WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kResFile, NotNull()))
      .WillOnce(DoAll(SetArgPointee<1>("12"), Return(true)));

  StatusOr<int64> statusor = controller_->GetMaxFdUsage();
  ASSERT_OK(statusor);
  EXPECT_EQ(12, statusor.ValueOrDie());
}

TEST_F(RLimitControllerTest, GetMaxFdUsageNotFound) {
  const string kResFile =
      JoinPath(kMountPoint, KernelFiles::RLimit::kFdMaxUsage);

  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK)).WillRepeatedly(Return(1));

  EXPECT_ERROR_CODE(NOT_FOUND, controller_->GetMaxFdUsage());
}

TEST_F(RLimitControllerTest, GetMaxFdUsageFails) {
  const string kResFile =
      JoinPath(kMountPoint, KernelFiles::RLimit::kFdMaxUsage);

  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK)).WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kResFile, NotNull()))
      .WillOnce(Return(false));

  EXPECT_NOT_OK(controller_->GetMaxFdUsage());
}

TEST_F(RLimitControllerTest, GetFdFailCountSuccess) {
  const string kResFile =
      JoinPath(kMountPoint, KernelFiles::RLimit::kFdFailCount);

  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK)).WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kResFile, NotNull()))
      .WillOnce(DoAll(SetArgPointee<1>("12"), Return(true)));

  StatusOr<int64> statusor = controller_->GetFdFailCount();
  ASSERT_OK(statusor);
  EXPECT_EQ(12, statusor.ValueOrDie());
}

TEST_F(RLimitControllerTest, GetFdFailCountNotFound) {
  const string kResFile =
      JoinPath(kMountPoint, KernelFiles::RLimit::kFdFailCount);

  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK)).WillRepeatedly(Return(1));

  EXPECT_ERROR_CODE(NOT_FOUND, controller_->GetFdFailCount());
}

TEST_F(RLimitControllerTest, GetFdFailCountFails) {
  const string kResFile =
      JoinPath(kMountPoint, KernelFiles::RLimit::kFdFailCount);

  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK)).WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kResFile, NotNull()))
      .WillOnce(Return(false));

  EXPECT_NOT_OK(controller_->GetFdFailCount());
}

}  // namespace lmctfy
}  // namespace containers
