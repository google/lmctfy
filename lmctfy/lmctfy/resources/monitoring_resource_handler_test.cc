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

#include "lmctfy/resources/monitoring_resource_handler.h"

#include <memory>
#include <vector>

#include "system_api/kernel_api_mock.h"
#include "lmctfy/controllers/cgroup_factory_mock.h"
#include "lmctfy/controllers/eventfd_notifications_mock.h"
#include "lmctfy/controllers/perf_controller_mock.h"
#include "util/errors_test_util.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::system_api::KernelAPIMock;
using ::std::unique_ptr;
using ::std::vector;
using ::testing::Return;
using ::testing::StrictMock;
using ::testing::_;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace lmctfy {

static const char kContainerName[] = "/test";
static const char kTaskInAllocName[] = "/sub";
static const char kTaskInAllocContainerName[] = "/test/sub";

class MonitoringResourceHandlerFactoryTest : public ::testing::Test {
 public:
  virtual void SetUp() {
    mock_kernel_.reset(new StrictMock<KernelAPIMock>());
    mock_controller_ = new StrictMockPerfController();
    mock_cgroup_factory_.reset(new NiceMockCgroupFactory());
    mock_controller_factory_ =
        new StrictMockPerfControllerFactory(mock_cgroup_factory_.get());
    factory_.reset(new MonitoringResourceHandlerFactory(
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
  MockPerfController *mock_controller_;
  MockPerfControllerFactory *mock_controller_factory_;
  unique_ptr<KernelAPIMock> mock_kernel_;
  unique_ptr<MonitoringResourceHandlerFactory> factory_;
  unique_ptr<MockCgroupFactory> mock_cgroup_factory_;
};

// Tests for New().

TEST_F(MonitoringResourceHandlerFactoryTest, NewSuccess) {
  unique_ptr<MockEventFdNotifications> mock_notifications(
      MockEventFdNotifications::NewStrict());

  EXPECT_CALL(*mock_cgroup_factory_, IsMounted(_))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cgroup_factory_, OwnsCgroup(_))
      .WillRepeatedly(Return(true));

  StatusOr<ResourceHandlerFactory *> statusor =
      MonitoringResourceHandlerFactory::New(mock_cgroup_factory_.get(),
                                            mock_kernel_.get(),
                                            mock_notifications.get());
  ASSERT_OK(statusor);
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  delete statusor.ValueOrDie();
}

TEST_F(MonitoringResourceHandlerFactoryTest, NewNotMounted) {
  unique_ptr<MockEventFdNotifications> mock_notifications(
      MockEventFdNotifications::NewStrict());

  EXPECT_CALL(*mock_cgroup_factory_, IsMounted(_))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*mock_cgroup_factory_, OwnsCgroup(_))
      .WillRepeatedly(Return(true));

  StatusOr<ResourceHandlerFactory *> statusor =
      MonitoringResourceHandlerFactory::New(mock_cgroup_factory_.get(),
                                            mock_kernel_.get(),
                                            mock_notifications.get());
  EXPECT_ERROR_CODE(::util::error::NOT_FOUND, statusor);
}

// Tests for Get().

TEST_F(MonitoringResourceHandlerFactoryTest, GetSuccess) {
  EXPECT_CALL(*mock_controller_factory_, Get(kContainerName))
      .WillRepeatedly(Return(mock_controller_));

  StatusOr<ResourceHandler *> statusor = CallGetResourceHandler(kContainerName);
  EXPECT_OK(statusor);
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  unique_ptr<ResourceHandler> handler(statusor.ValueOrDie());
  EXPECT_EQ(RESOURCE_MONITORING, handler->type());
  EXPECT_EQ(kContainerName, handler->container_name());
}

TEST_F(MonitoringResourceHandlerFactoryTest, GetTaskInAlloc) {
  EXPECT_CALL(*mock_controller_factory_, Get(kTaskInAllocName))
      .WillRepeatedly(Return(mock_controller_));

  StatusOr<ResourceHandler *> statusor =
      CallGetResourceHandler(kTaskInAllocContainerName);
  EXPECT_OK(statusor);
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  unique_ptr<ResourceHandler> handler(statusor.ValueOrDie());
  EXPECT_EQ(RESOURCE_MONITORING, handler->type());
  EXPECT_EQ(kTaskInAllocContainerName, handler->container_name());
}

TEST_F(MonitoringResourceHandlerFactoryTest, GetFails) {
  EXPECT_CALL(*mock_controller_factory_, Get(kContainerName))
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED, CallGetResourceHandler(kContainerName).status());

  // Mock controller was not used.
  delete mock_controller_;
}

// Tests for Create().

TEST_F(MonitoringResourceHandlerFactoryTest, CreateSuccess) {
  ContainerSpec spec;

  EXPECT_CALL(*mock_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_controller_));

  StatusOr<ResourceHandler *> statusor =
      CallCreateResourceHandler(kContainerName, spec);
  EXPECT_OK(statusor);
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  unique_ptr<ResourceHandler> handler(statusor.ValueOrDie());
  EXPECT_EQ(RESOURCE_MONITORING, handler->type());
  EXPECT_EQ(kContainerName, handler->container_name());
}

TEST_F(MonitoringResourceHandlerFactoryTest, CreateTaskInAlloc) {
  ContainerSpec spec;

  EXPECT_CALL(*mock_controller_factory_, Create(kTaskInAllocName))
      .WillRepeatedly(Return(mock_controller_));

  StatusOr<ResourceHandler *> statusor =
      CallCreateResourceHandler(kTaskInAllocContainerName, spec);
  EXPECT_OK(statusor);
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  unique_ptr<ResourceHandler> handler(statusor.ValueOrDie());
  EXPECT_EQ(RESOURCE_MONITORING, handler->type());
  EXPECT_EQ(kTaskInAllocContainerName, handler->container_name());
}

TEST_F(MonitoringResourceHandlerFactoryTest, CreateFails) {
  ContainerSpec spec;

  EXPECT_CALL(*mock_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED,
            CallCreateResourceHandler(kContainerName, spec).status());

  // Mock controller was not used.
  delete mock_controller_;
}

class MonitoringResourceHandlerTest : public ::testing::Test {
 public:
  MonitoringResourceHandlerTest()
      : kStatTypes({Container::STATS_SUMMARY, Container::STATS_FULL}) {}

  virtual void SetUp() {
    mock_kernel_.reset(new StrictMock<KernelAPIMock>());
    mock_memory_controller_ = new StrictMockPerfController();
    handler_.reset(new MonitoringResourceHandler(
        kContainerName, mock_kernel_.get(),
        mock_memory_controller_));
  }

 protected:
  const vector<Container::StatsType> kStatTypes;

  MockPerfController *mock_memory_controller_;
  unique_ptr<KernelAPIMock> mock_kernel_;
  unique_ptr<MonitoringResourceHandler> handler_;
};

TEST_F(MonitoringResourceHandlerTest, CreatesTest) {
  EXPECT_TRUE(handler_.get() != NULL);
}

}  // namespace lmctfy
}  // namespace containers
