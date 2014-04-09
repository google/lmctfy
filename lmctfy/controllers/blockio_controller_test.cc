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

#include "lmctfy/controllers/blockio_controller.h"

#include <memory>

#include "system_api/kernel_api_mock.h"
#include "file/base/path.h"
#include "lmctfy/controllers/eventfd_notifications_mock.h"
#include "lmctfy/kernel_files.h"
#include "util/errors_test_util.h"
#include "util/file_lines_test_util.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::system_api::KernelAPIMock;
using ::file::JoinPath;
using ::std::unique_ptr;
using ::testing::NotNull;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::StrictMock;
using ::testing::_;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace lmctfy {

static const char kMountPoint[] = "/dev/cgroup/io/test";

class BlockIoControllerTest : public ::testing::Test {
 public:
  void SetUp() override {
    mock_kernel_.reset(new StrictMock<KernelAPIMock>());
    mock_eventfd_notifications_.reset(MockEventFdNotifications::NewStrict());
    controller_.reset(new BlockIoController(kMountPoint, true,
                                            mock_kernel_.get(),
                                            mock_eventfd_notifications_.get()));
  }

 protected:
  unique_ptr<KernelAPIMock> mock_kernel_;
  unique_ptr<BlockIoController> controller_;
  unique_ptr<MockEventFdNotifications> mock_eventfd_notifications_;
};

TEST_F(BlockIoControllerTest, Type) {
  EXPECT_EQ(CGROUP_BLOCKIO, controller_->type());
}

TEST_F(BlockIoControllerTest, UpdateDefaultLimit) {
  const string kResFile = JoinPath(kMountPoint, KernelFiles::BlockIO::kWeight);

  string expected_out = "250";
  EXPECT_CALL(*mock_kernel_, SafeWriteResFile(expected_out, kResFile, NotNull(),
                                              NotNull())).WillOnce(Return(0));

  EXPECT_OK(controller_->UpdateDefaultLimit(25));
}

TEST_F(BlockIoControllerTest, UpdateDefaultLimitOutOfRange) {
  const string kResFile = JoinPath(kMountPoint, KernelFiles::BlockIO::kWeight);

  EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT,
                    controller_->UpdateDefaultLimit(0));

  EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT,
                    controller_->UpdateDefaultLimit(101));

  EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT,
                    controller_->UpdateDefaultLimit(1000));
}

TEST_F(BlockIoControllerTest, UpdateDefaultLimitFails) {
  const string kResFile = JoinPath(kMountPoint, KernelFiles::BlockIO::kWeight);

  string expected_out = "250";
  EXPECT_CALL(*mock_kernel_,
              SafeWriteResFile(expected_out, kResFile, NotNull(), NotNull()))
      .WillOnce(DoAll(SetArgPointee<3>(true), Return(0)));

  EXPECT_NOT_OK(controller_->UpdateDefaultLimit(25));
}

TEST_F(BlockIoControllerTest, GetDefaultLimitSuccess) {
  const string kResFile = JoinPath(kMountPoint, KernelFiles::BlockIO::kWeight);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK)).WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kResFile, NotNull()))
      .WillOnce(DoAll(SetArgPointee<1>("250"), Return(true)));
  StatusOr<uint32> statusor = controller_->GetDefaultLimit();
  ASSERT_TRUE(statusor.ok());
  EXPECT_EQ(25, statusor.ValueOrDie());
}

TEST_F(BlockIoControllerTest, GetDefaultLimitNotFound) {
  const string kResFile = JoinPath(kMountPoint, KernelFiles::BlockIO::kWeight);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK)).WillRepeatedly(Return(1));
  EXPECT_ERROR_CODE(::util::error::NOT_FOUND, controller_->GetDefaultLimit());
}

TEST_F(BlockIoControllerTest, GetDefaultLimitFails) {
  const string kResFile = JoinPath(kMountPoint, KernelFiles::BlockIO::kWeight);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK)).WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kResFile, NotNull()))
      .WillOnce(Return(false));
  EXPECT_NOT_OK(controller_->GetDefaultLimit());
}

}  // namespace lmctfy
}  // namespace containers
