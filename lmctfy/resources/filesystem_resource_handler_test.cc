// Copyright 2013 Google Inc. All Rights Reserved.
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

#include "lmctfy/resources/filesystem_resource_handler.h"

#include <memory>

#include "base/callback.h"
#include "system_api/kernel_api_mock.h"
#include "lmctfy/controllers/cgroup_factory_mock.h"
#include "lmctfy/controllers/eventfd_notifications_mock.h"
#include "lmctfy/controllers/rlimit_controller_mock.h"
#include "util/errors_test_util.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::system_api::KernelAPIMock;
using ::util::UnixGid;
using ::util::UnixUid;
using ::std::unique_ptr;
using ::std::vector;
using ::testing::Return;
using ::testing::StrictMock;
using ::testing::_;
using ::util::Status;
using ::util::StatusOr;
using ::util::error::INVALID_ARGUMENT;
using ::util::error::NOT_FOUND;

namespace containers {
namespace lmctfy {

static const char kContainerName[] = "/test";
static const char kHierarchicalContainerName[] = "/alloc/test";

class FilesystemResourceHandlerFactoryTest : public ::testing::Test {
 public:
  FilesystemResourceHandlerFactoryTest()
      : owner_uid_(UnixUid(10)), owner_gid_(UnixGid(11)) {}

  virtual void SetUp() {
    mock_kernel_.reset(new StrictMock<KernelAPIMock>());
    mock_controller_ = new StrictMockRLimitController();
    mock_cgroup_factory_.reset(new NiceMockCgroupFactory());
    mock_controller_factory_ =
        new StrictMockRLimitControllerFactory(mock_cgroup_factory_.get());
    factory_.reset(new FilesystemResourceHandlerFactory(
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
  const UnixUid owner_uid_;
  const UnixGid owner_gid_;

  MockRLimitController *mock_controller_;
  MockRLimitControllerFactory *mock_controller_factory_;
  unique_ptr<MockCgroupFactory> mock_cgroup_factory_;
  unique_ptr<KernelAPIMock> mock_kernel_;
  unique_ptr<FilesystemResourceHandlerFactory> factory_;
};

// Tests for New().

TEST_F(FilesystemResourceHandlerFactoryTest, NewSuccess) {
  unique_ptr<MockEventFdNotifications> mock_notifications(
      MockEventFdNotifications::NewStrict());

  EXPECT_CALL(*mock_cgroup_factory_, IsMounted(_))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cgroup_factory_, OwnsCgroup(_))
      .WillRepeatedly(Return(true));

  StatusOr<ResourceHandlerFactory *> statusor =
      FilesystemResourceHandlerFactory::New(mock_cgroup_factory_.get(),
                                        mock_kernel_.get(),
                                        mock_notifications.get());
  ASSERT_OK(statusor);
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  delete statusor.ValueOrDie();
}

TEST_F(FilesystemResourceHandlerFactoryTest, NewNotMounted) {
  unique_ptr<MockEventFdNotifications> mock_notifications(
      MockEventFdNotifications::NewStrict());

  EXPECT_CALL(*mock_cgroup_factory_, IsMounted(_))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*mock_cgroup_factory_, OwnsCgroup(_))
      .WillRepeatedly(Return(true));

  StatusOr<ResourceHandlerFactory *> statusor =
      FilesystemResourceHandlerFactory::New(mock_cgroup_factory_.get(),
                                        mock_kernel_.get(),
                                        mock_notifications.get());
  EXPECT_ERROR_CODE(::util::error::NOT_FOUND, statusor);
}

// Tests for Get().

TEST_F(FilesystemResourceHandlerFactoryTest, GetSuccess) {
  EXPECT_CALL(*mock_controller_factory_, Get(kContainerName))
      .WillRepeatedly(Return(mock_controller_));

  StatusOr<ResourceHandler *> statusor = CallGetResourceHandler(kContainerName);
  EXPECT_OK(statusor);
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  unique_ptr<ResourceHandler> handler(statusor.ValueOrDie());
  EXPECT_EQ(RESOURCE_FILESYSTEM, handler->type());
  EXPECT_EQ(kContainerName, handler->container_name());
}

TEST_F(FilesystemResourceHandlerFactoryTest, GetFails) {
  EXPECT_CALL(*mock_controller_factory_, Get(kContainerName))
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED, CallGetResourceHandler(kContainerName).status());

  // Mock controller was not used.
  delete mock_controller_;
}

TEST_F(FilesystemResourceHandlerFactoryTest, HierarchicalGetSuccess) {
  EXPECT_CALL(*mock_controller_factory_, Get(kContainerName))
      .WillRepeatedly(Return(mock_controller_));

  StatusOr<ResourceHandler *> statusor = CallGetResourceHandler(
      kHierarchicalContainerName);
  EXPECT_OK(statusor);
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  unique_ptr<ResourceHandler> handler(statusor.ValueOrDie());
  EXPECT_EQ(RESOURCE_FILESYSTEM, handler->type());
  EXPECT_EQ(kHierarchicalContainerName, handler->container_name());
}

// Tests for Create().

TEST_F(FilesystemResourceHandlerFactoryTest, CreateSuccess) {
  ContainerSpec spec;
  spec.set_owner(owner_uid_.value());
  spec.set_owner_group(owner_gid_.value());

  EXPECT_CALL(*mock_controller_factory_,
              Create(kContainerName, owner_uid_, owner_gid_))
      .WillRepeatedly(Return(mock_controller_));

  StatusOr<ResourceHandler *> statusor =
      CallCreateResourceHandler(kContainerName, spec);
  EXPECT_OK(statusor);
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  unique_ptr<ResourceHandler> handler(statusor.ValueOrDie());
  EXPECT_EQ(RESOURCE_FILESYSTEM, handler->type());
  EXPECT_EQ(kContainerName, handler->container_name());
}

TEST_F(FilesystemResourceHandlerFactoryTest, CreateFails) {
  ContainerSpec spec;
  spec.set_owner(owner_uid_.value());
  spec.set_owner_group(owner_gid_.value());

  EXPECT_CALL(*mock_controller_factory_,
              Create(kContainerName, owner_uid_, owner_gid_))
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED,
            CallCreateResourceHandler(kContainerName, spec).status());

  // Mock controller was not used.
  delete mock_controller_;
}

// Hierarchical container name is passed in. Controller sees a flat name.
TEST_F(FilesystemResourceHandlerFactoryTest, CreateHierarchicalSuccess) {
  ContainerSpec spec;
  spec.set_owner(owner_uid_.value());
  spec.set_owner_group(owner_gid_.value());

  EXPECT_CALL(*mock_controller_factory_,
              Create(kContainerName, owner_uid_, owner_gid_))
      .WillRepeatedly(Return(mock_controller_));

  StatusOr<ResourceHandler *> statusor =
      CallCreateResourceHandler(kHierarchicalContainerName, spec);
  EXPECT_OK(statusor);
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  unique_ptr<ResourceHandler> handler(statusor.ValueOrDie());
  EXPECT_EQ(RESOURCE_FILESYSTEM, handler->type());
  EXPECT_EQ(kHierarchicalContainerName, handler->container_name());
}

class FilesystemResourceHandlerTest : public ::testing::Test {
 public:
  FilesystemResourceHandlerTest()
      : kStatTypes({Container::STATS_SUMMARY, Container::STATS_FULL}) {}

  virtual void SetUp() {
    mock_kernel_.reset(new StrictMock<KernelAPIMock>());
    mock_rlimit_controller_ = new StrictMockRLimitController();
    handler_.reset(new FilesystemResourceHandler(
        kContainerName, mock_kernel_.get(),
        mock_rlimit_controller_));
  }

 protected:
  const vector<Container::StatsType> kStatTypes;

  MockRLimitController *mock_rlimit_controller_;
  unique_ptr<KernelAPIMock> mock_kernel_;
  unique_ptr<FilesystemResourceHandler> handler_;
};

TEST_F(FilesystemResourceHandlerTest, Spec) {
  ContainerSpec spec;
  EXPECT_CALL(*mock_rlimit_controller_, GetFdLimit()).WillOnce(Return(10));
  EXPECT_EQ(Status::OK, handler_->Spec(&spec));
  EXPECT_EQ(10, spec.filesystem().fd_limit());
}

TEST_F(FilesystemResourceHandlerTest, SpecFailure) {
  ContainerSpec spec;
  EXPECT_CALL(*mock_rlimit_controller_, GetFdLimit())
      .WillOnce(Return(Status(NOT_FOUND, "")));
  EXPECT_ERROR_CODE(NOT_FOUND, handler_->Spec(&spec));
}

void DoNothingTakeStatus(Status) {}

TEST_F(FilesystemResourceHandlerTest, RegisterNotification) {
  EXPECT_ERROR_CODE(NOT_FOUND,
                    handler_->RegisterNotification(EventSpec(),
                                                   NewPermanentCallback(
                                                       &DoNothingTakeStatus)));
}

TEST_F(FilesystemResourceHandlerTest, DoUpdate) {
  ContainerSpec spec;
  spec.mutable_filesystem()->set_fd_limit(13);
  EXPECT_CALL(*mock_rlimit_controller_, SetFdLimit(13))
      .WillOnce(Return(Status::OK));
  EXPECT_EQ(Status::OK, handler_->DoUpdate(spec));
}

TEST_F(FilesystemResourceHandlerTest, DoUpdateFailure) {
  ContainerSpec spec;
  spec.mutable_filesystem()->set_fd_limit(13);
  EXPECT_CALL(*mock_rlimit_controller_, SetFdLimit(13))
      .WillOnce(Return(Status(NOT_FOUND, "")));
  EXPECT_ERROR_CODE(NOT_FOUND, handler_->DoUpdate(spec));
}

TEST_F(FilesystemResourceHandlerTest, VerifyFullSpecOk) {
  ContainerSpec spec;
  spec.mutable_filesystem()->set_fd_limit(10);
  EXPECT_EQ(Status::OK, handler_->VerifyFullSpec(spec));
}

TEST_F(FilesystemResourceHandlerTest, VerifyFullSpecNoLimit) {
  ContainerSpec spec;
  EXPECT_ERROR_CODE(INVALID_ARGUMENT, handler_->VerifyFullSpec(spec));
}

}  // namespace lmctfy
}  // namespace containers
