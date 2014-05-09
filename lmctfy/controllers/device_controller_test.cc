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

#include "lmctfy/controllers/device_controller.h"

#include <memory>

#include "system_api/kernel_api_mock.h"
#include "file/base/path.h"
#include "lmctfy/controllers/eventfd_notifications_mock.h"
#include "lmctfy/kernel_files.h"
#include "util/errors_test_util.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::system_api::KernelAPIMock;
using ::file::JoinPath;
using ::std::unique_ptr;
#include "util/testing/equals_initialized_proto.h"
using ::testing::EqualsInitializedProto;
using ::testing::NotNull;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::StrEq;
using ::util::Status;
using ::util::StatusOr;
using ::util::error::FAILED_PRECONDITION;
using ::util::error::INTERNAL;
using ::util::error::INVALID_ARGUMENT;
using ::util::error::NOT_FOUND;


namespace containers {
namespace lmctfy {

static const char kCgroupPath[] = "/dev/cgroup/device/test";
static const char kHierarchyPath[] = "/test";

#define EXPECT_PROTOBUF_EQ(b1, b2) EXPECT_THAT(b2, EqualsInitializedProto(b1))

class DeviceControllerTest : public ::testing::Test {
 public:
  virtual void SetUp() {
    mock_kernel_.reset(new ::testing::StrictMock<KernelAPIMock>());
    mock_eventfd_notifications_.reset(MockEventFdNotifications::NewStrict());
    controller_.reset(
        new DeviceController(kHierarchyPath, kCgroupPath, false,
                             mock_kernel_.get(),
                             mock_eventfd_notifications_.get()));
  }

 protected:
  unique_ptr<KernelAPIMock> mock_kernel_;
  unique_ptr<MockEventFdNotifications> mock_eventfd_notifications_;
  unique_ptr<DeviceController> controller_;
};

TEST_F(DeviceControllerTest, TestType) {
  EXPECT_EQ(CGROUP_DEVICE, controller_->type());
}

TEST_F(DeviceControllerTest, AllowAllDevices) {
  DeviceSpec::DeviceRestrictionsSet rules;
  DeviceSpec::DeviceRestrictions *rule = rules.add_restrictions();
  rule->set_type(DeviceSpec::DEVICE_ALL);
  rule->add_access(DeviceSpec::READ);
  rule->add_access(DeviceSpec::WRITE);
  rule->add_access(DeviceSpec::MKNOD);
  rule->set_permission(DeviceSpec::ALLOW);
  // Leave major and minor numbers unset to include all devices.
  const string kExpectedString = "a *:* rwm";
  const string resource_file = JoinPath(kCgroupPath,
                                        KernelFiles::Device::kDevicesAllow);
  EXPECT_CALL(*mock_kernel_, SafeWriteResFile(kExpectedString, resource_file,
                                              NotNull(), NotNull()))
              .WillOnce(Return(0));
  EXPECT_OK(controller_->SetRestrictions(rules));
}

TEST_F(DeviceControllerTest, AllowBlockDevices) {
  DeviceSpec::DeviceRestrictionsSet rules;
  DeviceSpec::DeviceRestrictions *rule = rules.add_restrictions();
  rule->set_type(DeviceSpec::DEVICE_BLOCK);
  rule->add_access(DeviceSpec::READ);
  rule->add_access(DeviceSpec::WRITE);
  rule->add_access(DeviceSpec::MKNOD);
  rule->set_major(1);
  rule->set_minor(8);
  rule->set_permission(DeviceSpec::ALLOW);
  const string kExpectedString = "b 1:8 rwm";
  const string resource_file = JoinPath(kCgroupPath,
                                        KernelFiles::Device::kDevicesAllow);
  EXPECT_CALL(*mock_kernel_, SafeWriteResFile(kExpectedString, resource_file,
                                              NotNull(), NotNull()))
              .WillOnce(Return(0));
  EXPECT_OK(controller_->SetRestrictions(rules));
}

TEST_F(DeviceControllerTest, AllowCharDevices) {
  DeviceSpec::DeviceRestrictionsSet rules;
  DeviceSpec::DeviceRestrictions *rule = rules.add_restrictions();
  rule->set_type(DeviceSpec::DEVICE_CHAR);
  rule->add_access(DeviceSpec::READ);
  rule->add_access(DeviceSpec::WRITE);
  rule->add_access(DeviceSpec::MKNOD);
  rule->set_major(1);
  rule->set_minor(8);
  rule->set_permission(DeviceSpec::ALLOW);
  const string kExpectedString = "c 1:8 rwm";
  const string resource_file = JoinPath(kCgroupPath,
                                        KernelFiles::Device::kDevicesAllow);
  EXPECT_CALL(*mock_kernel_, SafeWriteResFile(kExpectedString, resource_file,
                                              NotNull(), NotNull()))
              .WillOnce(Return(0));
  EXPECT_OK(controller_->SetRestrictions(rules));
}

TEST_F(DeviceControllerTest, AllowReadOnly) {
  DeviceSpec::DeviceRestrictionsSet rules;
  DeviceSpec::DeviceRestrictions *rule = rules.add_restrictions();
  rule->set_type(DeviceSpec::DEVICE_CHAR);
  rule->add_access(DeviceSpec::READ);
  rule->set_major(1);
  rule->set_minor(8);
  rule->set_permission(DeviceSpec::ALLOW);
  const string kExpectedString = "c 1:8 r";
  const string resource_file = JoinPath(kCgroupPath,
                                        KernelFiles::Device::kDevicesAllow);
  EXPECT_CALL(*mock_kernel_, SafeWriteResFile(kExpectedString, resource_file,
                                              NotNull(), NotNull()))
              .WillOnce(Return(0));
  EXPECT_OK(controller_->SetRestrictions(rules));
}

TEST_F(DeviceControllerTest, AllowAllMinorDevices) {
  DeviceSpec::DeviceRestrictionsSet rules;
  DeviceSpec::DeviceRestrictions *rule = rules.add_restrictions();
  rule->set_type(DeviceSpec::DEVICE_CHAR);
  rule->add_access(DeviceSpec::READ);
  rule->add_access(DeviceSpec::WRITE);
  rule->add_access(DeviceSpec::MKNOD);
  // Specifying only major blocks all minor devices.
  rule->set_major(1);
  rule->set_permission(DeviceSpec::ALLOW);
  const string kExpectedString = "c 1:* rwm";
  const string resource_file = JoinPath(kCgroupPath,
                                        KernelFiles::Device::kDevicesAllow);
  EXPECT_CALL(*mock_kernel_, SafeWriteResFile(kExpectedString, resource_file,
                                              NotNull(), NotNull()))
              .WillOnce(Return(0));
  EXPECT_OK(controller_->SetRestrictions(rules));
}

TEST_F(DeviceControllerTest, AllowMultipleDevices) {
  DeviceSpec::DeviceRestrictionsSet rules;
  DeviceSpec::DeviceRestrictions *rule = rules.add_restrictions();
  rule->set_type(DeviceSpec::DEVICE_CHAR);
  rule->add_access(DeviceSpec::READ);
  rule->add_access(DeviceSpec::WRITE);
  rule->add_access(DeviceSpec::MKNOD);
  rule->set_major(1);
  rule->set_minor(8);
  rule->set_permission(DeviceSpec::ALLOW);
  DeviceSpec::DeviceRestrictions *rule_two = rules.add_restrictions();
  rule_two->set_type(DeviceSpec::DEVICE_BLOCK);
  rule_two->add_access(DeviceSpec::READ);
  rule_two->add_access(DeviceSpec::WRITE);
  rule_two->set_major(1);
  rule_two->set_minor(9);
  rule_two->set_permission(DeviceSpec::ALLOW);
  const string kExpectedStringOne = "c 1:8 rwm";
  const string kExpectedStringTwo = "b 1:9 rw";
  const string resource_file = JoinPath(kCgroupPath,
                                        KernelFiles::Device::kDevicesAllow);
  EXPECT_CALL(*mock_kernel_, SafeWriteResFile(kExpectedStringOne,
                                              resource_file,
                                              NotNull(), NotNull()))
              .WillOnce(Return(0));
  EXPECT_CALL(*mock_kernel_, SafeWriteResFile(kExpectedStringTwo,
                                              resource_file,
                                              NotNull(), NotNull()))
              .WillOnce(Return(0));
  EXPECT_OK(controller_->SetRestrictions(rules));
}

TEST_F(DeviceControllerTest, MixedAllowDenyDevices) {
  DeviceSpec::DeviceRestrictionsSet rules;
  DeviceSpec::DeviceRestrictions *rule = rules.add_restrictions();
  rule->set_type(DeviceSpec::DEVICE_CHAR);
  rule->add_access(DeviceSpec::READ);
  rule->add_access(DeviceSpec::WRITE);
  rule->add_access(DeviceSpec::MKNOD);
  rule->set_major(1);
  rule->set_minor(8);
  rule->set_permission(DeviceSpec::DENY);
  DeviceSpec::DeviceRestrictions *rule_two = rules.add_restrictions();
  rule_two->set_type(DeviceSpec::DEVICE_BLOCK);
  rule_two->add_access(DeviceSpec::READ);
  rule_two->add_access(DeviceSpec::WRITE);
  rule_two->set_major(1);
  rule_two->set_minor(9);
  rule_two->set_permission(DeviceSpec::ALLOW);
  const string kExpectedStringOne = "c 1:8 rwm";
  const string kExpectedStringTwo = "b 1:9 rw";
  const string allow_file = JoinPath(kCgroupPath,
                                     KernelFiles::Device::kDevicesAllow);
  const string deny_file = JoinPath(kCgroupPath,
                                    KernelFiles::Device::kDevicesDeny);
  EXPECT_CALL(*mock_kernel_, SafeWriteResFile(kExpectedStringOne, deny_file,
                                              NotNull(), NotNull()))
              .WillOnce(Return(0));
  EXPECT_CALL(*mock_kernel_, SafeWriteResFile(kExpectedStringTwo, allow_file,
                                              NotNull(), NotNull()))
              .WillOnce(Return(0));
  EXPECT_OK(controller_->SetRestrictions(rules));
}

TEST_F(DeviceControllerTest, MissingType) {
  DeviceSpec::DeviceRestrictionsSet rules;
  DeviceSpec::DeviceRestrictions *rule = rules.add_restrictions();
  rule->add_access(DeviceSpec::READ);
  rule->add_access(DeviceSpec::WRITE);
  rule->add_access(DeviceSpec::MKNOD);
  rule->set_major(1);
  rule->set_minor(8);
  rule->set_permission(DeviceSpec::ALLOW);
  EXPECT_ERROR_CODE(INVALID_ARGUMENT, controller_->SetRestrictions(rules));
}

TEST_F(DeviceControllerTest, MissingPermission) {
  DeviceSpec::DeviceRestrictionsSet rules;
  DeviceSpec::DeviceRestrictions *rule = rules.add_restrictions();
  rule->set_type(DeviceSpec::DEVICE_BLOCK);
  rule->add_access(DeviceSpec::READ);
  rule->add_access(DeviceSpec::WRITE);
  rule->add_access(DeviceSpec::MKNOD);
  rule->set_major(1);
  rule->set_minor(8);
  EXPECT_ERROR_CODE(INVALID_ARGUMENT, controller_->SetRestrictions(rules));
}


TEST_F(DeviceControllerTest, MissingAccess) {
  DeviceSpec::DeviceRestrictionsSet rules;
  DeviceSpec::DeviceRestrictions *rule = rules.add_restrictions();
  rule->set_type(DeviceSpec::DEVICE_BLOCK);
  rule->set_major(1);
  rule->set_minor(8);
  rule->set_permission(DeviceSpec::ALLOW);
  EXPECT_ERROR_CODE(INVALID_ARGUMENT, controller_->SetRestrictions(rules));
}

TEST_F(DeviceControllerTest, InvalidAccess) {
  DeviceSpec::DeviceRestrictionsSet rules;
  DeviceSpec::DeviceRestrictions *rule = rules.add_restrictions();
  rule->set_type(DeviceSpec::DEVICE_BLOCK);
  rule->set_major(1);
  rule->set_minor(8);
  rule->add_access(DeviceSpec::READ);
  rule->add_access(DeviceSpec::READ);
  rule->add_access(DeviceSpec::WRITE);
  rule->add_access(DeviceSpec::MKNOD);
  rule->set_permission(DeviceSpec::ALLOW);
  EXPECT_ERROR_CODE(INVALID_ARGUMENT, controller_->SetRestrictions(rules));
}

TEST_F(DeviceControllerTest, RepeatedAccessIgnored) {
  DeviceSpec::DeviceRestrictionsSet rules;
  DeviceSpec::DeviceRestrictions *rule = rules.add_restrictions();
  rule->set_type(DeviceSpec::DEVICE_CHAR);
  rule->add_access(DeviceSpec::READ);
  rule->add_access(DeviceSpec::READ);
  rule->add_access(DeviceSpec::MKNOD);
  rule->set_major(1);
  rule->set_minor(8);
  rule->set_permission(DeviceSpec::DENY);
  const string kExpectedString = "c 1:8 rm";
  const string resource_file = JoinPath(kCgroupPath,
                                        KernelFiles::Device::kDevicesDeny);
  EXPECT_CALL(*mock_kernel_, SafeWriteResFile(kExpectedString, resource_file,
                                              NotNull(), NotNull()))
              .WillOnce(Return(0));
  EXPECT_OK(controller_->SetRestrictions(rules));
}

TEST_F(DeviceControllerTest, DenyMultipleDevices) {
  DeviceSpec::DeviceRestrictionsSet rules;
  DeviceSpec::DeviceRestrictions *rule = rules.add_restrictions();
  rule->set_type(DeviceSpec::DEVICE_CHAR);
  rule->add_access(DeviceSpec::READ);
  rule->add_access(DeviceSpec::WRITE);
  rule->add_access(DeviceSpec::MKNOD);
  rule->set_major(1);
  rule->set_minor(8);
  rule->set_permission(DeviceSpec::DENY);
  DeviceSpec::DeviceRestrictions *rule_two = rules.add_restrictions();
  rule_two->set_type(DeviceSpec::DEVICE_BLOCK);
  rule_two->add_access(DeviceSpec::READ);
  rule_two->add_access(DeviceSpec::WRITE);
  rule_two->set_major(1);
  rule_two->set_minor(9);
  rule_two->set_permission(DeviceSpec::DENY);
  const string kExpectedStringOne = "c 1:8 rwm";
  const string kExpectedStringTwo = "b 1:9 rw";
  const string resource_file = JoinPath(kCgroupPath,
                                        KernelFiles::Device::kDevicesDeny);
  EXPECT_CALL(*mock_kernel_, SafeWriteResFile(kExpectedStringOne,
                                              resource_file,
                                              NotNull(), NotNull()))
              .WillOnce(Return(0));
  EXPECT_CALL(*mock_kernel_, SafeWriteResFile(kExpectedStringTwo,
                                              resource_file,
                                              NotNull(), NotNull()))
              .WillOnce(Return(0));
  EXPECT_OK(controller_->SetRestrictions(rules));
}


TEST_F(DeviceControllerTest, DenyOneCharDevice) {
  DeviceSpec::DeviceRestrictionsSet rules;
  DeviceSpec::DeviceRestrictions *rule = rules.add_restrictions();
  rule->set_type(DeviceSpec::DEVICE_CHAR);
  rule->add_access(DeviceSpec::READ);
  rule->add_access(DeviceSpec::WRITE);
  rule->add_access(DeviceSpec::MKNOD);
  rule->set_major(1);
  rule->set_minor(8);
  rule->set_permission(DeviceSpec::DENY);
  const string kExpectedString = "c 1:8 rwm";
  const string resource_file = JoinPath(kCgroupPath,
                                        KernelFiles::Device::kDevicesDeny);
  EXPECT_CALL(*mock_kernel_, SafeWriteResFile(kExpectedString, resource_file,
                                              NotNull(), NotNull()))
              .WillOnce(Return(0));
  EXPECT_OK(controller_->SetRestrictions(rules));
}

TEST_F(DeviceControllerTest, DenyOneBlockDevice) {
  DeviceSpec::DeviceRestrictionsSet rules;
  DeviceSpec::DeviceRestrictions *rule = rules.add_restrictions();
  rule->set_type(DeviceSpec::DEVICE_BLOCK);
  rule->add_access(DeviceSpec::READ);
  rule->add_access(DeviceSpec::WRITE);
  rule->add_access(DeviceSpec::MKNOD);
  rule->set_major(1);
  rule->set_minor(8);
  rule->set_permission(DeviceSpec::DENY);
  const string kExpectedString = "b 1:8 rwm";
  const string resource_file = JoinPath(kCgroupPath,
                                        KernelFiles::Device::kDevicesDeny);
  EXPECT_CALL(*mock_kernel_, SafeWriteResFile(kExpectedString, resource_file,
                                              NotNull(), NotNull()))
              .WillOnce(Return(0));
  EXPECT_OK(controller_->SetRestrictions(rules));
}

TEST_F(DeviceControllerTest, DenyAllDevices) {
  DeviceSpec::DeviceRestrictionsSet rules;
  DeviceSpec::DeviceRestrictions *rule = rules.add_restrictions();
  rule->set_type(DeviceSpec::DEVICE_ALL);
  rule->add_access(DeviceSpec::READ);
  rule->add_access(DeviceSpec::WRITE);
  rule->add_access(DeviceSpec::MKNOD);
  rule->set_permission(DeviceSpec::DENY);
  // Leave major and minor numbers unset to include all devices.
  const string kExpectedString = "a *:* rwm";
  const string resource_file = JoinPath(kCgroupPath,
                                        KernelFiles::Device::kDevicesDeny);
  EXPECT_CALL(*mock_kernel_, SafeWriteResFile(kExpectedString, resource_file,
                                              NotNull(), NotNull()))
              .WillOnce(Return(0));
  EXPECT_OK(controller_->SetRestrictions(rules));
}

TEST_F(DeviceControllerTest, TestStateAllDevicesDenied) {
  const string kResFile = JoinPath(kCgroupPath,
                                   KernelFiles::Device::kDevicesList);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK)).WillRepeatedly(Return(0));
  // device list returns empty string.
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kResFile, NotNull()))
      .WillOnce(DoAll(SetArgPointee<1>(""), Return(true)));
  StatusOr<DeviceSpec::DeviceRestrictionsSet> statusor =
      controller_->GetState();
  ASSERT_OK(statusor);
  DeviceSpec::DeviceRestrictionsSet restriction_set = statusor.ValueOrDie();
  EXPECT_EQ(1, restriction_set.restrictions_size());
  const DeviceSpec::DeviceRestrictions &rule = restriction_set.restrictions(0);
  DeviceSpec::DeviceRestrictions expected_rule;
  expected_rule.set_type(DeviceSpec::DEVICE_ALL);
  expected_rule.add_access(DeviceSpec::READ);
  expected_rule.add_access(DeviceSpec::WRITE);
  expected_rule.add_access(DeviceSpec::MKNOD);
  expected_rule.set_permission(DeviceSpec::DENY);
  EXPECT_PROTOBUF_EQ(expected_rule, rule);
}

TEST_F(DeviceControllerTest, TestStateAllDevicesAccess) {
  const string kResFile = JoinPath(kCgroupPath,
                                   KernelFiles::Device::kDevicesList);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK)).WillRepeatedly(Return(0));
  // All devices are allowed.
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kResFile, NotNull()))
      .WillOnce(DoAll(SetArgPointee<1>("a *:* rwm"), Return(true)));
  StatusOr<DeviceSpec::DeviceRestrictionsSet> statusor =
      controller_->GetState();
  ASSERT_OK(statusor);
  DeviceSpec::DeviceRestrictionsSet restriction_set = statusor.ValueOrDie();
  EXPECT_EQ(1, restriction_set.restrictions_size());
  const DeviceSpec::DeviceRestrictions &rule = restriction_set.restrictions(0);
  DeviceSpec::DeviceRestrictions expected_rule;
  expected_rule.set_type(DeviceSpec::DEVICE_ALL);
  expected_rule.add_access(DeviceSpec::MKNOD);
  expected_rule.add_access(DeviceSpec::READ);
  expected_rule.add_access(DeviceSpec::WRITE);
  expected_rule.set_permission(DeviceSpec::ALLOW);
  EXPECT_PROTOBUF_EQ(expected_rule, rule);
}

TEST_F(DeviceControllerTest, TestStateNotFound) {
  const string kResFile = JoinPath(kCgroupPath,
                                   KernelFiles::Device::kDevicesList);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK)).WillRepeatedly(Return(1));
  EXPECT_ERROR_CODE(NOT_FOUND, controller_->GetState());
}

TEST_F(DeviceControllerTest, TestStateReadFails) {
  const string kResFile = JoinPath(kCgroupPath,
                                   KernelFiles::Device::kDevicesList);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK)).WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kResFile, NotNull()))
      .WillOnce(Return(false));
  EXPECT_ERROR_CODE(FAILED_PRECONDITION, controller_->GetState());
}

TEST_F(DeviceControllerTest, TestStateMalformedRule) {
  const string kResFile = JoinPath(kCgroupPath,
                                   KernelFiles::Device::kDevicesList);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK)).WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kResFile, NotNull()))
      .WillOnce(DoAll(SetArgPointee<1>("c 8:* rwm allow\n"), Return(true)));
  EXPECT_ERROR_CODE(INTERNAL, controller_->GetState());
}

TEST_F(DeviceControllerTest, TestStateIncompleteRule) {
  const string kResFile = JoinPath(kCgroupPath,
                                   KernelFiles::Device::kDevicesList);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK)).WillRepeatedly(Return(0));
  // Missing major:minor device numbers.
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kResFile, NotNull()))
      .WillOnce(DoAll(SetArgPointee<1>("c rwm\n"), Return(true)));
  EXPECT_ERROR_CODE(INTERNAL, controller_->GetState());
}

TEST_F(DeviceControllerTest, TestStateUnknownType) {
  const string kResFile = JoinPath(kCgroupPath,
                                   KernelFiles::Device::kDevicesList);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK)).WillRepeatedly(Return(0));
  // Unknown device type.
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kResFile, NotNull()))
      .WillOnce(DoAll(SetArgPointee<1>("d 1:8 rwm\n"), Return(true)));
  EXPECT_ERROR_CODE(INTERNAL, controller_->GetState());
}

TEST_F(DeviceControllerTest, TestStateMissingType) {
  const string kResFile = JoinPath(kCgroupPath,
                                   KernelFiles::Device::kDevicesList);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK)).WillRepeatedly(Return(0));
  // Missing device type.
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kResFile, NotNull()))
      .WillOnce(DoAll(SetArgPointee<1>("1:8 rwm\n"), Return(true)));
  EXPECT_ERROR_CODE(INTERNAL, controller_->GetState());
}

TEST_F(DeviceControllerTest, TestStateUnknownDeviceNumbers) {
  const string kResFile = JoinPath(kCgroupPath,
                                   KernelFiles::Device::kDevicesList);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK)).WillRepeatedly(Return(0));
  // Unknown device number.
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kResFile, NotNull()))
      .WillOnce(DoAll(SetArgPointee<1>("b x:8 rwm\n"), Return(true)));
  EXPECT_ERROR_CODE(INTERNAL, controller_->GetState());
}

TEST_F(DeviceControllerTest, TestStateMissingAccessType) {
  const string kResFile = JoinPath(kCgroupPath,
                                   KernelFiles::Device::kDevicesList);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK)).WillRepeatedly(Return(0));
  // Missing access type.
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kResFile, NotNull()))
      .WillOnce(DoAll(SetArgPointee<1>("b 1:8\n"), Return(true)));
  EXPECT_ERROR_CODE(INTERNAL, controller_->GetState());
}

TEST_F(DeviceControllerTest, TesStateUnknownAccessType) {
  const string kResFile = JoinPath(kCgroupPath,
                                   KernelFiles::Device::kDevicesList);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK)).WillRepeatedly(Return(0));
  // Unknown access type.
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kResFile, NotNull()))
      .WillOnce(DoAll(SetArgPointee<1>("b 1:8 rwx\n"), Return(true)));
  EXPECT_ERROR_CODE(INTERNAL, controller_->GetState());
}

TEST_F(DeviceControllerTest, TestStateMultipleDeviceAccess) {
  const string kResFile = JoinPath(kCgroupPath,
                                   KernelFiles::Device::kDevicesList);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK)).WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kResFile, NotNull()))
      .WillOnce(DoAll(SetArgPointee<1>("c 8:* rwm\nb 1:* rwm\nb 1:9 rm"),
                      Return(true)));
  StatusOr<DeviceSpec::DeviceRestrictionsSet> statusor =
      controller_->GetState();
  ASSERT_OK(statusor);
  DeviceSpec::DeviceRestrictionsSet restriction_set = statusor.ValueOrDie();
  EXPECT_EQ(3, restriction_set.restrictions_size());
  DeviceSpec::DeviceRestrictionsSet expected_ruleset;
  DeviceSpec::DeviceRestrictions *expected_rule =
      expected_ruleset.add_restrictions();
  expected_rule->set_type(DeviceSpec::DEVICE_CHAR);
  expected_rule->set_major(8);
  expected_rule->add_access(DeviceSpec::MKNOD);
  expected_rule->add_access(DeviceSpec::READ);
  expected_rule->add_access(DeviceSpec::WRITE);
  expected_rule->set_permission(DeviceSpec::ALLOW);

  expected_rule = expected_ruleset.add_restrictions();
  expected_rule->set_type(DeviceSpec::DEVICE_BLOCK);
  expected_rule->set_major(1);
  expected_rule->add_access(DeviceSpec::MKNOD);
  expected_rule->add_access(DeviceSpec::READ);
  expected_rule->add_access(DeviceSpec::WRITE);
  expected_rule->set_permission(DeviceSpec::ALLOW);

  expected_rule = expected_ruleset.add_restrictions();
  expected_rule->set_type(DeviceSpec::DEVICE_BLOCK);
  expected_rule->set_major(1);
  expected_rule->set_minor(9);
  expected_rule->add_access(DeviceSpec::MKNOD);
  expected_rule->add_access(DeviceSpec::READ);
  expected_rule->set_permission(DeviceSpec::ALLOW);

  EXPECT_PROTOBUF_EQ(expected_ruleset, restriction_set);
}

TEST_F(DeviceControllerTest, VerifyRestrictionSuccess) {
  DeviceSpec::DeviceRestrictions rule;
  rule.set_type(DeviceSpec::DEVICE_BLOCK);
  rule.set_permission(DeviceSpec::ALLOW);
  rule.add_access(DeviceSpec::MKNOD);
  rule.set_major(1);
  rule.set_minor(8);
  EXPECT_OK(controller_->VerifyRestriction(rule));
}

TEST_F(DeviceControllerTest, VerifyRestrictionMissingNumbersOK) {
  DeviceSpec::DeviceRestrictions rule;
  rule.set_type(DeviceSpec::DEVICE_BLOCK);
  rule.set_permission(DeviceSpec::ALLOW);
  rule.add_access(DeviceSpec::MKNOD);
  // major/minor numbers are unset.
  EXPECT_OK(controller_->VerifyRestriction(rule));
}


TEST_F(DeviceControllerTest, VerifyRestrictionEmpty) {
  DeviceSpec::DeviceRestrictions rule;
  EXPECT_ERROR_CODE(INVALID_ARGUMENT, controller_->VerifyRestriction(rule));
}

TEST_F(DeviceControllerTest, VerifyRestrictionMissingPermission) {
  DeviceSpec::DeviceRestrictions rule;
  rule.set_type(DeviceSpec::DEVICE_BLOCK);
  rule.add_access(DeviceSpec::MKNOD);
  rule.set_major(1);
  rule.set_minor(8);
  EXPECT_ERROR_CODE(INVALID_ARGUMENT, controller_->VerifyRestriction(rule));
}

TEST_F(DeviceControllerTest, VerifyRestrictionMissingType) {
  DeviceSpec::DeviceRestrictions rule;
  rule.set_permission(DeviceSpec::ALLOW);
  rule.add_access(DeviceSpec::MKNOD);
  rule.set_major(1);
  rule.set_minor(8);
  EXPECT_ERROR_CODE(INVALID_ARGUMENT, controller_->VerifyRestriction(rule));
}

TEST_F(DeviceControllerTest, VerifyRestrictionMissingAccess) {
  DeviceSpec::DeviceRestrictions rule;
  rule.set_permission(DeviceSpec::ALLOW);
  rule.set_type(DeviceSpec::DEVICE_BLOCK);
  rule.set_major(1);
  rule.set_minor(8);
  EXPECT_ERROR_CODE(INVALID_ARGUMENT, controller_->VerifyRestriction(rule));
}

TEST_F(DeviceControllerTest, VerifyRestrictionTooManyAccess) {
  DeviceSpec::DeviceRestrictions rule;
  rule.set_permission(DeviceSpec::ALLOW);
  rule.set_type(DeviceSpec::DEVICE_BLOCK);
  rule.add_access(DeviceSpec::MKNOD);
  rule.add_access(DeviceSpec::MKNOD);
  rule.add_access(DeviceSpec::READ);
  rule.add_access(DeviceSpec::WRITE);
  rule.set_major(1);
  rule.set_minor(8);
  EXPECT_ERROR_CODE(INVALID_ARGUMENT, controller_->VerifyRestriction(rule));
}

}  // namespace lmctfy
}  // namespace containers
