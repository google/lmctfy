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

#include "lmctfy/resources/device_resource_handler.h"

#include <limits>
#include <memory>

#include "base/callback.h"
#include "system_api/kernel_api_mock.h"
#include "lmctfy/controllers/cgroup_factory_mock.h"
#include "lmctfy/controllers/eventfd_notifications_mock.h"
#include "lmctfy/controllers/device_controller_mock.h"
#include "util/errors_test_util.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::system_api::KernelAPIMock;
using ::std::unique_ptr;
using ::std::vector;
#include "util/testing/equals_initialized_proto.h"
using ::testing::EqualsInitializedProto;
using ::testing::Return;
using ::testing::StrictMock;
using ::testing::_;
using ::util::Status;
using ::util::StatusOr;
using ::util::error::INTERNAL;
using ::util::error::INVALID_ARGUMENT;
using ::util::error::NOT_FOUND;

namespace containers {
namespace lmctfy {

static const char kContainerName[] = "/test";
static const char kHierarchicalContainerName[] = "/alloc/test";

class DeviceResourceHandlerFactoryTest : public ::testing::Test {
 public:
  void SetUp() override {
    mock_kernel_.reset(new StrictMock<KernelAPIMock>());
    mock_controller_ = new StrictMockDeviceController();
    mock_cgroup_factory_.reset(new NiceMockCgroupFactory());
    mock_controller_factory_ =
        new StrictMockDeviceControllerFactory(mock_cgroup_factory_.get());
    factory_.reset(new DeviceResourceHandlerFactory(
        mock_controller_factory_, mock_cgroup_factory_.get(),
        mock_kernel_.get()));
  }

  // Wrappers over private methods for testing.

  StatusOr<ResourceHandler *> CallGetResourceHandler(
      const string &container_name) {
    return factory_->GetResourceHandler(container_name);
  }

  StatusOr<ResourceHandler *> CallCreateResourceHandler(
      const string &container_name, const ContainerSpec &spec) {
    return factory_->CreateResourceHandler(container_name, spec);
  }

 protected:
  MockDeviceController *mock_controller_;
  MockDeviceControllerFactory *mock_controller_factory_;
  unique_ptr<MockCgroupFactory> mock_cgroup_factory_;
  unique_ptr<KernelAPIMock> mock_kernel_;
  unique_ptr<DeviceResourceHandlerFactory> factory_;
};

// Tests for New().

TEST_F(DeviceResourceHandlerFactoryTest, NewSuccess) {
  unique_ptr<MockEventFdNotifications> mock_notifications(
      MockEventFdNotifications::NewStrict());

  EXPECT_CALL(*mock_cgroup_factory_, IsMounted(_))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cgroup_factory_, OwnsCgroup(_))
      .WillRepeatedly(Return(true));

  StatusOr<ResourceHandlerFactory *> statusor =
      DeviceResourceHandlerFactory::New(mock_cgroup_factory_.get(),
                                        mock_kernel_.get(),
                                        mock_notifications.get());
  ASSERT_OK(statusor);
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  delete statusor.ValueOrDie();
}

TEST_F(DeviceResourceHandlerFactoryTest, NewNotMounted) {
  unique_ptr<MockEventFdNotifications> mock_notifications(
      MockEventFdNotifications::NewStrict());

  EXPECT_CALL(*mock_cgroup_factory_, IsMounted(_))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*mock_cgroup_factory_, OwnsCgroup(_))
      .WillRepeatedly(Return(true));

  StatusOr<ResourceHandlerFactory *> statusor =
      DeviceResourceHandlerFactory::New(mock_cgroup_factory_.get(),
                                        mock_kernel_.get(),
                                        mock_notifications.get());
  EXPECT_ERROR_CODE(NOT_FOUND, statusor);
}

// Tests for Get().

TEST_F(DeviceResourceHandlerFactoryTest, GetSuccess) {
  EXPECT_CALL(*mock_controller_factory_, Get(kContainerName))
      .WillRepeatedly(Return(mock_controller_));

  StatusOr<ResourceHandler *> statusor = CallGetResourceHandler(kContainerName);
  ASSERT_OK(statusor);
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  unique_ptr<ResourceHandler> handler(statusor.ValueOrDie());
  EXPECT_EQ(RESOURCE_DEVICE, handler->type());
  EXPECT_EQ(kContainerName, handler->container_name());
}

TEST_F(DeviceResourceHandlerFactoryTest, GetFails) {
  EXPECT_CALL(*mock_controller_factory_, Get(kContainerName))
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED, CallGetResourceHandler(kContainerName).status());

  // Mock controller was not used.
  delete mock_controller_;
}

TEST_F(DeviceResourceHandlerFactoryTest, HierarchicalGetSuccess) {
  EXPECT_CALL(*mock_controller_factory_, Get(kHierarchicalContainerName))
      .WillRepeatedly(Return(mock_controller_));

  StatusOr<ResourceHandler *> statusor = CallGetResourceHandler(
      kHierarchicalContainerName);
  ASSERT_OK(statusor);
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  unique_ptr<ResourceHandler> handler(statusor.ValueOrDie());
  EXPECT_EQ(RESOURCE_DEVICE, handler->type());
  EXPECT_EQ(kHierarchicalContainerName, handler->container_name());
}

// Tests for Create().

TEST_F(DeviceResourceHandlerFactoryTest, CreateSuccess) {
  ContainerSpec spec;

  EXPECT_CALL(*mock_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_controller_));

  StatusOr<ResourceHandler *> statusor =
      CallCreateResourceHandler(kContainerName, spec);
  ASSERT_OK(statusor);
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  unique_ptr<ResourceHandler> handler(statusor.ValueOrDie());
  EXPECT_EQ(RESOURCE_DEVICE, handler->type());
  EXPECT_EQ(kContainerName, handler->container_name());
}

TEST_F(DeviceResourceHandlerFactoryTest, CreateFails) {
  ContainerSpec spec;

  EXPECT_CALL(*mock_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED,
            CallCreateResourceHandler(kContainerName, spec).status());

  // Mock controller was not used.
  delete mock_controller_;
}

// Hierarchical container name is passed in. Controller sees a flat name.
TEST_F(DeviceResourceHandlerFactoryTest, CreateHierarchicalSuccess) {
  ContainerSpec spec;

  EXPECT_CALL(*mock_controller_factory_, Create(kHierarchicalContainerName))
      .WillRepeatedly(Return(mock_controller_));

  StatusOr<ResourceHandler *> statusor =
      CallCreateResourceHandler(kHierarchicalContainerName, spec);
  ASSERT_OK(statusor);
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  unique_ptr<ResourceHandler> handler(statusor.ValueOrDie());
  EXPECT_EQ(RESOURCE_DEVICE, handler->type());
  EXPECT_EQ(kHierarchicalContainerName, handler->container_name());
}

class DeviceResourceHandlerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    mock_kernel_.reset(new StrictMock<KernelAPIMock>());
    mock_device_controller_ = new StrictMockDeviceController();
    handler_.reset(new DeviceResourceHandler(
        kContainerName, mock_kernel_.get(),
        mock_device_controller_));
    DeviceSpec::DeviceRestrictions *restriction =
        restrictions_set_.add_restrictions();
    restriction->set_type(DeviceSpec::DEVICE_CHAR);
    restriction->set_permission(DeviceSpec::DENY);
    restriction->set_major(1);
    restriction->set_minor(8);
    restriction->add_access(DeviceSpec::MKNOD);
  }

  MockDeviceController *mock_device_controller_;
  unique_ptr<KernelAPIMock> mock_kernel_;
  unique_ptr<DeviceResourceHandler> handler_;
  DeviceSpec::DeviceRestrictionsSet restrictions_set_;
};

TEST_F(DeviceResourceHandlerTest, SpecSuccess) {
  ContainerSpec spec;
  EXPECT_CALL(*mock_device_controller_, GetState())
      .WillOnce(Return(restrictions_set_));
  ASSERT_OK(handler_->Spec(&spec));
  EXPECT_THAT(spec.device().restrictions_set(),
              ::testing::EqualsInitializedProto(restrictions_set_));
}

TEST_F(DeviceResourceHandlerTest, SpecFailure) {
  ContainerSpec spec;
  EXPECT_CALL(*mock_device_controller_, GetState())
      .WillOnce(Return(Status(NOT_FOUND, "")));
  EXPECT_ERROR_CODE(NOT_FOUND, handler_->Spec(&spec));
}

void DoNothingTakeStatus(Status) {}

TEST_F(DeviceResourceHandlerTest, RegisterNotification) {
  EXPECT_ERROR_CODE(NOT_FOUND,
                    handler_->RegisterNotification(EventSpec(),
                                                   NewPermanentCallback(
                                                       &DoNothingTakeStatus)));
}

TEST_F(DeviceResourceHandlerTest, DoUpdate) {
  ContainerSpec spec;
  DeviceSpec::DeviceRestrictionsSet *restrictions_set =
      spec.mutable_device()->mutable_restrictions_set();
  *restrictions_set = restrictions_set_;
  EXPECT_CALL(*mock_device_controller_,
              SetRestrictions(EqualsInitializedProto(restrictions_set_)))
      .WillOnce(Return(Status::OK));
  ASSERT_OK(handler_->DoUpdate(spec));
}

TEST_F(DeviceResourceHandlerTest, DoUpdateEmptySuccess) {
  ContainerSpec spec;
  ASSERT_OK(handler_->DoUpdate(spec));
}

TEST_F(DeviceResourceHandlerTest, DoUpdateFailure) {
  ContainerSpec spec;
  DeviceSpec::DeviceRestrictionsSet *restrictions_set =
      spec.mutable_device()->mutable_restrictions_set();
  *restrictions_set = restrictions_set_;
  EXPECT_CALL(*mock_device_controller_,
              SetRestrictions(EqualsInitializedProto(restrictions_set_)))
      .WillOnce(Return(Status(NOT_FOUND, "")));
  EXPECT_ERROR_CODE(NOT_FOUND, handler_->DoUpdate(spec));
}

TEST_F(DeviceResourceHandlerTest, VerifyFullSpecEmptyOk) {
  ContainerSpec spec;
  ASSERT_OK(handler_->VerifyFullSpec(spec));
}

TEST_F(DeviceResourceHandlerTest, VerifyFullSpecOk) {
  ContainerSpec spec;
  DeviceSpec::DeviceRestrictionsSet *restrictions_set =
      spec.mutable_device()->mutable_restrictions_set();
  *restrictions_set = restrictions_set_;
  EXPECT_CALL(*mock_device_controller_, VerifyRestriction(
      EqualsInitializedProto(restrictions_set_.restrictions(0))))
      .WillOnce(Return(Status::OK));
  ASSERT_OK(handler_->VerifyFullSpec(spec));
}

TEST_F(DeviceResourceHandlerTest, VerifyFullSpecFailed) {
  ContainerSpec spec;
  DeviceSpec::DeviceRestrictionsSet *restrictions_set =
      spec.mutable_device()->mutable_restrictions_set();
  *restrictions_set = restrictions_set_;
  EXPECT_CALL(*mock_device_controller_, VerifyRestriction(
      EqualsInitializedProto(restrictions_set_.restrictions(0))))
      .WillOnce(Return(Status(INVALID_ARGUMENT, "")));
  EXPECT_ERROR_CODE(INVALID_ARGUMENT, handler_->VerifyFullSpec(spec));
}

TEST_F(DeviceResourceHandlerTest, StatsOK) {
  ContainerStats output;
  const vector<Container::StatsType> kStatTypes(
      {Container::STATS_SUMMARY, Container::STATS_FULL});
  for (Container::StatsType type : kStatTypes) {
    ASSERT_OK(handler_->Stats(type, &output));
  }
}

}  // namespace lmctfy
}  // namespace containers
