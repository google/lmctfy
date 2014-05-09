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
using ::util::FileLinesTestUtil;
using ::std::unique_ptr;
#include "util/testing/equals_initialized_proto.h"
using ::testing::EqualsInitializedProto;
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
static const char kHierarchyPath[] = "/test";

#define EXPECT_PROTOBUF_EQ(b1, b2) EXPECT_THAT(b2, EqualsInitializedProto(b1))

class BlockIoControllerTest : public ::testing::Test {
 public:
  void SetUp() override {
    mock_kernel_.reset(new StrictMock<KernelAPIMock>());
    mock_eventfd_notifications_.reset(MockEventFdNotifications::NewStrict());
    controller_.reset(new BlockIoController(kHierarchyPath, kMountPoint, true,
                                            mock_kernel_.get(),
                                            mock_eventfd_notifications_.get()));
  }

 protected:
  void SetDeviceLimit(BlockIoSpec::DeviceLimit *device_limit,
                      int32 major, int32 minor, int64 limit) {
    device_limit->set_limit(limit);
    device_limit->mutable_device()->set_major(major);
    device_limit->mutable_device()->set_minor(minor);
  }

  void SetThrottlingType(BlockIoSpec::MaxLimit *max_limit,
                         BlockIoSpec::OpType op, BlockIoSpec::LimitType type) {
    max_limit->set_op_type(op);
    max_limit->set_limit_type(type);
  }

  FileLinesTestUtil mock_file_lines_;
  unique_ptr<KernelAPIMock> mock_kernel_;
  unique_ptr<BlockIoController> controller_;
  unique_ptr<MockEventFdNotifications> mock_eventfd_notifications_;
};

TEST_F(BlockIoControllerTest, Type) {
  EXPECT_EQ(CGROUP_BLOCKIO, controller_->type());
}

TEST_F(BlockIoControllerTest, UpdateDefaultLimit) {
  const string kResFile = JoinPath(kMountPoint, KernelFiles::BlockIO::kWeight);

  const string kExpectedOut = "250";
  EXPECT_CALL(*mock_kernel_, SafeWriteResFile(kExpectedOut, kResFile, NotNull(),
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

  const string kExpectedOut = "250";
  EXPECT_CALL(*mock_kernel_,
              SafeWriteResFile(kExpectedOut, kResFile, NotNull(), NotNull()))
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

TEST_F(BlockIoControllerTest, UpdatePerDeviceLimitSuccess) {
  const string kResFile = JoinPath(kMountPoint,
                                   KernelFiles::BlockIO::kPerDeviceWeight);
  const string kExpectedOutFirst = "8:0 200";
  const string kExpectedOutSecond = "8:16 400";
  BlockIoSpec::DeviceLimitSet limits_set;
  BlockIoSpec::DeviceLimit *limit = limits_set.add_device_limits();
  SetDeviceLimit(limit, 8, 0, 20);
  limit = limits_set.add_device_limits();
  SetDeviceLimit(limit, 8, 16, 40);
  EXPECT_CALL(*mock_kernel_, SafeWriteResFile(kExpectedOutFirst, kResFile,
                                              NotNull(), NotNull()))
      .WillOnce(Return(0));
  EXPECT_CALL(*mock_kernel_, SafeWriteResFile(kExpectedOutSecond, kResFile,
                                              NotNull(), NotNull()))
      .WillOnce(Return(0));
  EXPECT_OK(controller_->UpdatePerDeviceLimit(limits_set));
}

TEST_F(BlockIoControllerTest, UpdatePerDeviceLimitMalformed) {
  BlockIoSpec::DeviceLimitSet limits_set;
  BlockIoSpec::DeviceLimit *limit = limits_set.add_device_limits();
  // Missing major number.
  limit->mutable_device()->set_minor(0);
  limit->set_limit(20);
  EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT,
                    controller_->UpdatePerDeviceLimit(limits_set));
  limit->mutable_device()->set_major(8);
  // Missing minor
  limit->mutable_device()->clear_minor();
  EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT,
                    controller_->UpdatePerDeviceLimit(limits_set));
  // Missing limit
  limit->mutable_device()->set_minor(0);
  limit->clear_limit();
  EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT,
                    controller_->UpdatePerDeviceLimit(limits_set));
  // Limit out of range
  limit->set_limit(200);
  EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT,
                    controller_->UpdatePerDeviceLimit(limits_set));
}

TEST_F(BlockIoControllerTest, UpdatePerDeviceLimitFails) {
  const string kResFile = JoinPath(kMountPoint,
                                   KernelFiles::BlockIO::kPerDeviceWeight);
  BlockIoSpec::DeviceLimitSet limits_set;
  BlockIoSpec::DeviceLimit *limit = limits_set.add_device_limits();
  SetDeviceLimit(limit, 8, 0, 20);

  const string kExpectedOut = "8:0 200";
  EXPECT_CALL(*mock_kernel_,
              SafeWriteResFile(kExpectedOut, kResFile, NotNull(), NotNull()))
      .WillOnce(DoAll(SetArgPointee<3>(true), Return(0)));
  EXPECT_NOT_OK(controller_->UpdatePerDeviceLimit(limits_set));
}

TEST_F(BlockIoControllerTest, GetDeviceLimitsSuccess) {
  const string kResFile = JoinPath(kMountPoint,
                                   KernelFiles::BlockIO::kPerDeviceWeight);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK)).WillRepeatedly(Return(0));
  const vector<string> kOutputLines = { "8:0 500\n" };
  mock_file_lines_.ExpectFileLines(kResFile, kOutputLines);
  BlockIoSpec::DeviceLimitSet expected_device_set;
  BlockIoSpec::DeviceLimit *device_limit =
      expected_device_set.add_device_limits();
  SetDeviceLimit(device_limit, 8, 0, 50);
  StatusOr<BlockIoSpec::DeviceLimitSet> statusor =
      controller_->GetDeviceLimits();
  ASSERT_TRUE(statusor.ok());
  EXPECT_PROTOBUF_EQ(expected_device_set, statusor.ValueOrDie());
}

TEST_F(BlockIoControllerTest, GetDeviceLimitsSuccessMultipleDevices) {
  const string kResFile = JoinPath(kMountPoint,
                                   KernelFiles::BlockIO::kPerDeviceWeight);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK)).WillRepeatedly(Return(0));
  const vector<string> kOutputLines = { "8:0 500\n", "8:16 200" };
  mock_file_lines_.ExpectFileLines(kResFile, kOutputLines);
  BlockIoSpec::DeviceLimitSet expected_device_set;
  BlockIoSpec::DeviceLimit *device_limit =
      expected_device_set.add_device_limits();
  SetDeviceLimit(device_limit, 8, 0, 50);
  device_limit = expected_device_set.add_device_limits();
  SetDeviceLimit(device_limit, 8, 16, 20);
  StatusOr<BlockIoSpec::DeviceLimitSet> statusor =
      controller_->GetDeviceLimits();
  ASSERT_TRUE(statusor.ok());
  EXPECT_PROTOBUF_EQ(expected_device_set, statusor.ValueOrDie());
}

TEST_F(BlockIoControllerTest, GetDeviceLimitsNotFound) {
  const string kResFile = JoinPath(kMountPoint,
                                   KernelFiles::BlockIO::kPerDeviceWeight);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK)).WillRepeatedly(Return(1));
  EXPECT_ERROR_CODE(::util::error::NOT_FOUND, controller_->GetDeviceLimits());
}

TEST_F(BlockIoControllerTest, GetDeviceLimitsEmpty) {
  const string kResFile = JoinPath(kMountPoint,
                                   KernelFiles::BlockIO::kPerDeviceWeight);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK)).WillRepeatedly(Return(0));
  const vector<string> kOutputLines = { "" };
  mock_file_lines_.ExpectFileLines(kResFile, kOutputLines);
  BlockIoSpec::DeviceLimitSet expected_device_set;
  StatusOr<BlockIoSpec::DeviceLimitSet> statusor =
      controller_->GetDeviceLimits();
  ASSERT_TRUE(statusor.ok());
  EXPECT_PROTOBUF_EQ(expected_device_set, statusor.ValueOrDie());
}

TEST_F(BlockIoControllerTest, GetDeviceLimitsIgnoreMalformedLines) {
  const string kResFile = JoinPath(kMountPoint,
                                   KernelFiles::BlockIO::kPerDeviceWeight);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK)).WillRepeatedly(Return(0));
  const vector<string> kOutputLines = { "8:0 500\n", "Not a valid output" };
  mock_file_lines_.ExpectFileLines(kResFile, kOutputLines);
  BlockIoSpec::DeviceLimitSet expected_device_set;
  BlockIoSpec::DeviceLimit *device_limit =
      expected_device_set.add_device_limits();
  SetDeviceLimit(device_limit, 8, 0, 50);
  StatusOr<BlockIoSpec::DeviceLimitSet> statusor =
      controller_->GetDeviceLimits();
  ASSERT_TRUE(statusor.ok());
  EXPECT_PROTOBUF_EQ(expected_device_set, statusor.ValueOrDie());
}

TEST_F(BlockIoControllerTest, UpdateMaxLimitSuccess) {
  const string kReadBpsFile = JoinPath(
      kMountPoint, KernelFiles::BlockIO::kMaxReadBytesPerSecond);
  const string kWriteBpsFile = JoinPath(
      kMountPoint, KernelFiles::BlockIO::kMaxWriteBytesPerSecond);
  const string kReadIopsFile = JoinPath(
      kMountPoint, KernelFiles::BlockIO::kMaxReadIoPerSecond);
  const string kWriteIopsFile = JoinPath(
      kMountPoint, KernelFiles::BlockIO::kMaxWriteIoPerSecond);
  const string kExpectedOutReadBps = "8:0 300";
  const string kExpectedOutWriteBps = "8:16 600";
  const string kExpectedOutReadIops = "8:32 900";
  const string kExpectedOutWriteIops = "8:48 100";
  BlockIoSpec::MaxLimitSet limits_set;
  BlockIoSpec::MaxLimit *max_limit = limits_set.add_max_limits();
  SetThrottlingType(max_limit, BlockIoSpec::READ,
                    BlockIoSpec::BYTES_PER_SECOND);
  BlockIoSpec::DeviceLimit *device_limit = max_limit->add_limits();
  SetDeviceLimit(device_limit, 8, 0, 300);
  max_limit = limits_set.add_max_limits();
  SetThrottlingType(max_limit, BlockIoSpec::WRITE,
                    BlockIoSpec::BYTES_PER_SECOND);
  device_limit = max_limit->add_limits();
  SetDeviceLimit(device_limit, 8, 16, 600);
  max_limit = limits_set.add_max_limits();
  SetThrottlingType(max_limit, BlockIoSpec::READ,
                    BlockIoSpec::IO_PER_SECOND);
  device_limit = max_limit->add_limits();
  SetDeviceLimit(device_limit, 8, 32, 900);
  max_limit = limits_set.add_max_limits();
  SetThrottlingType(max_limit, BlockIoSpec::WRITE,
                    BlockIoSpec::IO_PER_SECOND);
  device_limit = max_limit->add_limits();
  SetDeviceLimit(device_limit, 8, 48, 100);
  EXPECT_CALL(*mock_kernel_, SafeWriteResFile(kExpectedOutReadBps,
                                              kReadBpsFile,
                                              NotNull(), NotNull()))
      .WillOnce(Return(0));
  EXPECT_CALL(*mock_kernel_, SafeWriteResFile(kExpectedOutWriteBps,
                                              kWriteBpsFile,
                                              NotNull(), NotNull()))
      .WillOnce(Return(0));
  EXPECT_CALL(*mock_kernel_, SafeWriteResFile(kExpectedOutReadIops,
                                              kReadIopsFile,
                                              NotNull(), NotNull()))
      .WillOnce(Return(0));
  EXPECT_CALL(*mock_kernel_, SafeWriteResFile(kExpectedOutWriteIops,
                                              kWriteIopsFile,
                                              NotNull(), NotNull()))
      .WillOnce(Return(0));
  EXPECT_OK(controller_->UpdateMaxLimit(limits_set));
}

TEST_F(BlockIoControllerTest, UpdateMaxLimitMalformed) {
  BlockIoSpec::MaxLimitSet limits_set;
  BlockIoSpec::MaxLimit *max_limit = limits_set.add_max_limits();
  // Missing op type.
  max_limit->set_limit_type(BlockIoSpec::BYTES_PER_SECOND);
  BlockIoSpec::DeviceLimit *device_limit = max_limit->add_limits();
  SetDeviceLimit(device_limit, 300, 8, 0);
  EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT,
                    controller_->UpdateMaxLimit(limits_set));
  // Missing limit type.
  max_limit->set_op_type(BlockIoSpec::READ);
  max_limit->clear_limit_type();
  EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT,
                    controller_->UpdateMaxLimit(limits_set));
  // Missing major number.
  device_limit->set_limit(300);
  device_limit->mutable_device()->clear_major();
  EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT,
                    controller_->UpdateMaxLimit(limits_set));
  // Missing minor number.
  device_limit->mutable_device()->set_major(8);
  device_limit->mutable_device()->clear_minor();
  EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT,
                    controller_->UpdateMaxLimit(limits_set));
  // Missing limit.
  device_limit->mutable_device()->set_minor(0);
  device_limit->clear_limit();
  EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT,
                    controller_->UpdateMaxLimit(limits_set));
  // Missing device.
  max_limit->clear_limits();
  EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT,
                    controller_->UpdateMaxLimit(limits_set));
}

TEST_F(BlockIoControllerTest, UpdateMaxLimitFail) {
  const string kResFile = JoinPath(
      kMountPoint, KernelFiles::BlockIO::kMaxReadBytesPerSecond);
  BlockIoSpec::MaxLimitSet limits_set;
  BlockIoSpec::MaxLimit *max_limit = limits_set.add_max_limits();
  SetThrottlingType(max_limit, BlockIoSpec::READ,
                    BlockIoSpec::BYTES_PER_SECOND);
  BlockIoSpec::DeviceLimit *device_limit = max_limit->add_limits();
  SetDeviceLimit(device_limit, 8, 0, 300);
  const string kExpectedOut = "8:0 300";
  EXPECT_CALL(*mock_kernel_,
              SafeWriteResFile(kExpectedOut, kResFile, NotNull(), NotNull()))
      .WillOnce(DoAll(SetArgPointee<3>(true), Return(0)));
  EXPECT_NOT_OK(controller_->UpdateMaxLimit(limits_set));
}

TEST_F(BlockIoControllerTest, GetMaxLimitSuccess) {
  const string kReadBpsFile = JoinPath(
      kMountPoint, KernelFiles::BlockIO::kMaxReadBytesPerSecond);
  EXPECT_CALL(*mock_kernel_, Access(kReadBpsFile, F_OK))
      .WillRepeatedly(Return(0));
  const string kWriteBpsFile = JoinPath(
      kMountPoint, KernelFiles::BlockIO::kMaxWriteBytesPerSecond);
  EXPECT_CALL(*mock_kernel_, Access(kWriteBpsFile, F_OK))
      .WillRepeatedly(Return(0));
  const string kReadIopsFile = JoinPath(
      kMountPoint, KernelFiles::BlockIO::kMaxReadIoPerSecond);
  EXPECT_CALL(*mock_kernel_, Access(kReadIopsFile, F_OK))
      .WillRepeatedly(Return(0));
  const string kWriteIopsFile = JoinPath(
      kMountPoint, KernelFiles::BlockIO::kMaxWriteIoPerSecond);
  EXPECT_CALL(*mock_kernel_, Access(kWriteIopsFile, F_OK))
      .WillRepeatedly(Return(0));
  const vector<string> kOutReadBps = { "8:0 300", "8:16 200" };
  mock_file_lines_.ExpectFileLines(kReadBpsFile, kOutReadBps);
  const vector<string> kOutWriteBps = { "8:0 600", "8:16 400" };
  mock_file_lines_.ExpectFileLines(kWriteBpsFile, kOutWriteBps);
  const vector<string> kOutReadIops = { "8:0 900", "8:16 600" };
  mock_file_lines_.ExpectFileLines(kReadIopsFile, kOutReadIops);
  const vector<string> kOutWriteIops = { "8:0 100", "8:16 800" };
  mock_file_lines_.ExpectFileLines(kWriteIopsFile, kOutWriteIops);
  // Setup Expected Proto.
  BlockIoSpec::MaxLimitSet expected_limits_set;
  BlockIoSpec::MaxLimit *max_limit = expected_limits_set.add_max_limits();
  SetThrottlingType(max_limit, BlockIoSpec::READ, BlockIoSpec::IO_PER_SECOND);
  BlockIoSpec::DeviceLimit *device_limit = max_limit->add_limits();
  SetDeviceLimit(device_limit, 8, 0, 900);
  device_limit = max_limit->add_limits();
  SetDeviceLimit(device_limit, 8, 16, 600);
  max_limit = expected_limits_set.add_max_limits();
  SetThrottlingType(max_limit, BlockIoSpec::WRITE, BlockIoSpec::IO_PER_SECOND);
  device_limit = max_limit->add_limits();
  SetDeviceLimit(device_limit, 8, 0, 100);
  device_limit = max_limit->add_limits();
  SetDeviceLimit(device_limit, 8, 16, 800);
  max_limit = expected_limits_set.add_max_limits();
  SetThrottlingType(max_limit, BlockIoSpec::READ,
                    BlockIoSpec::BYTES_PER_SECOND);
  device_limit = max_limit->add_limits();
  SetDeviceLimit(device_limit, 8, 0, 300);
  device_limit = max_limit->add_limits();
  SetDeviceLimit(device_limit, 8, 16, 200);
  max_limit = expected_limits_set.add_max_limits();
  SetThrottlingType(max_limit, BlockIoSpec::WRITE,
                    BlockIoSpec::BYTES_PER_SECOND);
  device_limit = max_limit->add_limits();
  SetDeviceLimit(device_limit, 8, 0, 600);
  device_limit = max_limit->add_limits();
  SetDeviceLimit(device_limit, 8, 16, 400);
  StatusOr<BlockIoSpec::MaxLimitSet> statusor = controller_->GetMaxLimit();
  ASSERT_TRUE(statusor.ok());
  EXPECT_PROTOBUF_EQ(expected_limits_set, statusor.ValueOrDie());
}

TEST_F(BlockIoControllerTest, GetMaxLimitsNotFound) {
  const string kReadBpsFile = JoinPath(
      kMountPoint, KernelFiles::BlockIO::kMaxReadBytesPerSecond);
  const string kWriteBpsFile = JoinPath(
      kMountPoint, KernelFiles::BlockIO::kMaxWriteBytesPerSecond);
  const string kReadIopsFile = JoinPath(
      kMountPoint, KernelFiles::BlockIO::kMaxReadIoPerSecond);
  const string kWriteIopsFile = JoinPath(
      kMountPoint, KernelFiles::BlockIO::kMaxWriteIoPerSecond);

  const vector<string> kOut = { "8:0 300", "8:16 200" };

  EXPECT_CALL(*mock_kernel_, Access(kReadIopsFile, F_OK)).WillOnce(Return(1));
  EXPECT_ERROR_CODE(::util::error::NOT_FOUND, controller_->GetMaxLimit());
  EXPECT_CALL(*mock_kernel_, Access(kReadIopsFile, F_OK))
      .WillRepeatedly(Return(0));

  mock_file_lines_.ExpectFileLines(kReadIopsFile, kOut);
  EXPECT_CALL(*mock_kernel_, Access(kWriteIopsFile, F_OK)).WillOnce(Return(1));
  EXPECT_ERROR_CODE(::util::error::NOT_FOUND, controller_->GetMaxLimit());
  EXPECT_CALL(*mock_kernel_, Access(kWriteIopsFile, F_OK))
      .WillRepeatedly(Return(0));

  mock_file_lines_.ExpectFileLines(kReadIopsFile, kOut);
  mock_file_lines_.ExpectFileLines(kWriteIopsFile, kOut);
  EXPECT_CALL(*mock_kernel_, Access(kReadBpsFile, F_OK)).WillOnce(Return(1));
  EXPECT_ERROR_CODE(::util::error::NOT_FOUND, controller_->GetMaxLimit());
  EXPECT_CALL(*mock_kernel_, Access(kReadBpsFile, F_OK))
      .WillRepeatedly(Return(0));

  mock_file_lines_.ExpectFileLines(kReadIopsFile, kOut);
  mock_file_lines_.ExpectFileLines(kWriteIopsFile, kOut);
  mock_file_lines_.ExpectFileLines(kReadBpsFile, kOut);
  EXPECT_CALL(*mock_kernel_, Access(kWriteBpsFile, F_OK)).WillOnce(Return(1));
  EXPECT_ERROR_CODE(::util::error::NOT_FOUND, controller_->GetMaxLimit());
}

}  // namespace lmctfy
}  // namespace containers
