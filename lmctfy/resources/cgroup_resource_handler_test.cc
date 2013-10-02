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

#include "lmctfy/resources/cgroup_resource_handler.h"

#include <map>
#include <memory>
#include <vector>

#include "system_api/kernel_api_mock.h"
#include "lmctfy/controllers/cgroup_controller_mock.h"
#include "lmctfy/controllers/cgroup_factory_mock.h"
#include "lmctfy/resource_handler_mock.h"
#include "include/lmctfy.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "util/task/status.h"

namespace containers {
namespace lmctfy {
class CgroupController;
}  // namespace lmctfy
}  // namespace containers

using ::system_api::KernelAPIMock;
using ::std::map;
using ::std::unique_ptr;
using ::std::vector;
using ::testing::DoAll;
#include "util/testing/equals_initialized_proto.h"
using ::testing::EqualsInitializedProto;
using ::testing::NotNull;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::StrictMock;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace lmctfy {
namespace {

// Pass-through handler factory for CPU. Used for testing
// CgroupResourceHandlerFactory.
class TestCpuHandlerFactory : public CgroupResourceHandlerFactory {
 public:
  TestCpuHandlerFactory(ResourceType resource_type,
                        CgroupFactory *cgroup_factory, const KernelApi *kernel)
      : CgroupResourceHandlerFactory(resource_type, cgroup_factory, kernel),
        get_resource_handlers_status_(Status::CANCELLED),
        create_resource_handlers_status_(Status::CANCELLED) {}
  ~TestCpuHandlerFactory() {}

  StatusOr<ResourceHandler *> GetResourceHandler(
      const string &container_name) const {
    return get_resource_handlers_status_;
  }

  StatusOr<ResourceHandler *> CreateResourceHandler(
      const string &container_name, const ContainerSpec &spec) const {
    return create_resource_handlers_status_;
  }

  void set_get_resource_handlers_status(StatusOr<ResourceHandler *> status) {
    get_resource_handlers_status_ = status;
  }

  void set_create_resource_handlers_status(StatusOr<ResourceHandler *> status) {
    create_resource_handlers_status_ = status;
  }

 private:
  StatusOr<ResourceHandler *> get_resource_handlers_status_;
  StatusOr<ResourceHandler *> create_resource_handlers_status_;

  DISALLOW_COPY_AND_ASSIGN(TestCpuHandlerFactory);
};

// Pass-through handler for CPU. Used for testing CgroupResourceHandler.
class TestCpuHandler: public CgroupResourceHandler {
 public:
  TestCpuHandler(const string &container_name,
                 ResourceType resource_type,
                 const KernelApi *kernel,
                 const vector<CgroupController *> &controllers)
      : CgroupResourceHandler(container_name, resource_type, kernel,
                              controllers) {}
  ~TestCpuHandler() {}

  Status Update(const ContainerSpec &spec, Container::UpdatePolicy policy) {
    return Status::CANCELLED;
  }
  Status Stats(Container::StatsType type, ContainerStats *output) const {
    return Status::CANCELLED;
  }
  Status Spec(ContainerSpec *spec) const {
    return Status::CANCELLED;
  }
  StatusOr<Container::NotificationId> RegisterNotification(
      const EventSpec &spec, Callback1<Status> *callback) {
    return Status::CANCELLED;
  }

  // Wrappers to test CgroupResourceHandler.

  Status Destroy() {
    return CgroupResourceHandler::Destroy();
  }
  Status Enter(const vector<pid_t> &tids) {
    return CgroupResourceHandler::Enter(tids);
  }
  const map<CgroupHierarchy, CgroupController *> &controllers() {
    return controllers_;
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(TestCpuHandler);
};

static const char kContainerName[] = "/test";
static const char kCgroupMount[] = "/dev/cgroup";
static const char kMountPoint[] = "/dev/cgroup/cpu";

class CgroupResourceHandlerFactoryTest : public ::testing::Test {
 public:
  virtual void SetUp() {
    mock_kernel_.reset(new StrictMock<KernelAPIMock>());
    mock_cgroup_factory_.reset(new StrictMockCgroupFactory());
    factory_.reset(new TestCpuHandlerFactory(
        RESOURCE_CPU, mock_cgroup_factory_.get(), mock_kernel_.get()));
  }

 protected:
  unique_ptr<KernelAPIMock> mock_kernel_;
  unique_ptr<TestCpuHandlerFactory> factory_;
  unique_ptr<MockCgroupFactory> mock_cgroup_factory_;
};

// Tests for Get().

TEST_F(CgroupResourceHandlerFactoryTest, Get) {
  factory_->set_get_resource_handlers_status(
      new StrictMockResourceHandler(kContainerName, RESOURCE_CPU));

  StatusOr<ResourceHandler *> statusor = factory_->Get(kContainerName);
  ASSERT_TRUE(statusor.ok());
  unique_ptr<ResourceHandler> handler(statusor.ValueOrDie());
  EXPECT_NE(nullptr, handler.get());
  EXPECT_EQ(kContainerName, handler->container_name());
  EXPECT_EQ(RESOURCE_CPU, handler->type());
}

TEST_F(CgroupResourceHandlerFactoryTest, GetGetControllersFails) {
  EXPECT_EQ(Status::CANCELLED, factory_->Get(kContainerName).status());
}

// Tests for Create().

TEST_F(CgroupResourceHandlerFactoryTest, Create) {
  ContainerSpec spec;
  MockResourceHandler *mock_handler =
      new StrictMockResourceHandler(kContainerName, RESOURCE_CPU);
  factory_->set_create_resource_handlers_status(mock_handler);

  EXPECT_CALL(*mock_handler,
              Update(EqualsInitializedProto(spec), Container::UPDATE_REPLACE))
      .WillRepeatedly(Return(Status::OK));

  StatusOr<ResourceHandler *> statusor = factory_->Create(kContainerName, spec);
  ASSERT_TRUE(statusor.ok());
  unique_ptr<ResourceHandler> handler(statusor.ValueOrDie());
  EXPECT_NE(nullptr, handler.get());
  EXPECT_EQ(kContainerName, handler->container_name());
  EXPECT_EQ(RESOURCE_CPU, handler->type());
}

TEST_F(CgroupResourceHandlerFactoryTest, CreateCreateControllersFails) {
  ContainerSpec spec;
  EXPECT_EQ(Status::CANCELLED, factory_->Create(kContainerName, spec).status());
}

TEST_F(CgroupResourceHandlerFactoryTest, CreateUpdateFails) {
  ContainerSpec spec;
  MockResourceHandler *mock_handler =
      new StrictMockResourceHandler(kContainerName, RESOURCE_CPU);
  factory_->set_create_resource_handlers_status(mock_handler);

  EXPECT_CALL(*mock_handler,
              Update(EqualsInitializedProto(spec), Container::UPDATE_REPLACE))
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED, factory_->Create(kContainerName, spec).status());
}

class CgroupResourceHandlerTest : public ::testing::Test {
 public:
  virtual void SetUp() {
    mock_kernel_.reset(new StrictMock<KernelAPIMock>());
    mock_controller1_ =
        new StrictMockCgroupController(CGROUP_CPU, kMountPoint, true);
    mock_controller2_ =
        new StrictMockCgroupController(CGROUP_CPUACCT, kMountPoint, true);
    handler_.reset(new TestCpuHandler(
        kContainerName, RESOURCE_CPU, mock_kernel_.get(),
        vector<CgroupController *>({mock_controller1_, mock_controller2_})));
  }

 protected:
  MockCgroupController *mock_controller1_;
  MockCgroupController *mock_controller2_;
  unique_ptr<KernelAPIMock> mock_kernel_;
  unique_ptr<TestCpuHandler> handler_;
};

TEST_F(CgroupResourceHandlerTest, DestroySuccess) {
  EXPECT_CALL(*mock_controller1_, Destroy())
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_controller2_, Destroy())
      .WillOnce(Return(Status::OK));

  // Destroy deletes the handler.
  EXPECT_TRUE(handler_.release()->Destroy().ok());

  // The controllers are supposed to be deleted by Destroy().
  delete mock_controller1_;
  delete mock_controller2_;
}

TEST_F(CgroupResourceHandlerTest, DestroyFirstDestroyFails) {
  EXPECT_CALL(*mock_controller1_, Destroy())
      .WillOnce(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED, handler_->Destroy());
}

TEST_F(CgroupResourceHandlerTest, DestroySecondDestroyFails) {
  EXPECT_CALL(*mock_controller1_, Destroy())
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_controller2_, Destroy())
      .WillOnce(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED, handler_->Destroy());

  // The controllers are supposed to be deleted by Destroy().
  delete mock_controller1_;
}

TEST_F(CgroupResourceHandlerTest, EnterSuccess) {
  const vector<pid_t> tids = {11, 12};

  EXPECT_CALL(*mock_controller1_, Enter(11))
      .WillRepeatedly(Return(Status::OK));
  EXPECT_CALL(*mock_controller1_, Enter(12))
      .WillRepeatedly(Return(Status::OK));
  EXPECT_CALL(*mock_controller2_, Enter(11))
      .WillRepeatedly(Return(Status::OK));
  EXPECT_CALL(*mock_controller2_, Enter(12))
      .WillRepeatedly(Return(Status::OK));

  EXPECT_TRUE(handler_->Enter(tids).ok());
}

TEST_F(CgroupResourceHandlerTest, EnterEnterFails) {
  const vector<pid_t> tids = {11, 12};

  EXPECT_CALL(*mock_controller1_, Enter(11))
      .WillRepeatedly(Return(Status::CANCELLED));
  EXPECT_CALL(*mock_controller1_, Enter(12))
      .WillRepeatedly(Return(Status::OK));
  EXPECT_CALL(*mock_controller2_, Enter(11))
      .WillRepeatedly(Return(Status::OK));
  EXPECT_CALL(*mock_controller2_, Enter(12))
      .WillRepeatedly(Return(Status::OK));

  EXPECT_EQ(Status::CANCELLED, handler_->Enter(tids));
}

}  // namespace
}  // namespace lmctfy
}  // namespace containers
