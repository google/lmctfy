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

#include "lmctfy/resources/cpu_resource_handler.h"

#include <map>
#include <memory>
#include <vector>

#include "base/integral_types.h"
#include "system_api/kernel_api_mock.h"
#include "lmctfy/controllers/cgroup_factory_mock.h"
#include "lmctfy/controllers/cpu_controller.h"
#include "lmctfy/controllers/cpu_controller_mock.h"
#include "lmctfy/controllers/cpuacct_controller.h"
#include "lmctfy/controllers/cpuacct_controller_mock.h"
#include "lmctfy/controllers/cpuset_controller_mock.h"
#include "lmctfy/controllers/eventfd_notifications_mock.h"
#include "lmctfy/resource_handler.h"
#include "include/lmctfy.pb.h"
#include "util/safe_types/time.h"
#include "util/cpu_mask.h"
#include "util/cpu_mask_test_util.h"
#include "util/errors_test_util.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "util/task/codes.pb.h"

using ::system_api::KernelAPIMock;
using ::util::CpuMask;
using ::util::Nanoseconds;
using ::std::unique_ptr;
using ::std::vector;
using ::testing::Eq;
#include "util/testing/equals_initialized_proto.h"
using ::testing::EqualsInitializedProto;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Pointwise;
using ::testing::Return;
using ::testing::StrictMock;
using ::testing::_;
using ::util::Status;
using ::util::StatusOr;
using ::util::error::NOT_FOUND;

namespace containers {
namespace lmctfy {

static const char kContainerName[] = "/test";
static const char kBatchHierarchyPath[] = "/batch/test";

class CpuResourceHandlerFactoryTest : public ::testing::Test {
 public:
  virtual void SetUp() {
    SetUpFactory(true);
  }

  void SetUpFactory(bool cpuset_enabled) {
    mock_kernel_.reset(new StrictMock<KernelAPIMock>());
    mock_cgroup_factory_.reset(new NiceMockCgroupFactory());
    mock_cpu_controller_factory_ =
        new StrictMockCpuControllerFactory(mock_cgroup_factory_.get());
    mock_cpuacct_controller_factory_ =
        new StrictMockCpuAcctControllerFactory(mock_cgroup_factory_.get());

    mock_cpuset_controller_factory_ = nullptr;
    if (cpuset_enabled) {
      mock_cpuset_controller_factory_ =
          new StrictMockCpusetControllerFactory(mock_cgroup_factory_.get());
    }

    factory_.reset(new CpuResourceHandlerFactory(
        mock_cpu_controller_factory_, mock_cpuacct_controller_factory_,
        mock_cpuset_controller_factory_, mock_cgroup_factory_.get(),
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

  StatusOr<ResourceHandler *> CallCreate(
      const string &container_name, const ContainerSpec &spec) {
    return factory_->Create(container_name, spec);
  }

  Status CallInitMachine(const InitSpec &spec) {
    return factory_->InitMachine(spec);
  }

 protected:
  MockCpuControllerFactory *mock_cpu_controller_factory_;
  MockCpuAcctControllerFactory *mock_cpuacct_controller_factory_;
  MockCpusetControllerFactory *mock_cpuset_controller_factory_;
  unique_ptr<KernelAPIMock> mock_kernel_;
  unique_ptr<CpuResourceHandlerFactory> factory_;
  unique_ptr<MockCgroupFactory> mock_cgroup_factory_;
};

// Tests for New().

TEST_F(CpuResourceHandlerFactoryTest, NewSuccess) {
  unique_ptr<MockEventFdNotifications> mock_notifications(
      MockEventFdNotifications::NewStrict());

  EXPECT_CALL(*mock_cgroup_factory_, IsMounted(_))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cgroup_factory_, OwnsCgroup(_))
      .WillRepeatedly(Return(true));

  StatusOr<ResourceHandlerFactory *> statusor = CpuResourceHandlerFactory::New(
      mock_cgroup_factory_.get(), mock_kernel_.get(), mock_notifications.get());
  ASSERT_OK(statusor);
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  delete statusor.ValueOrDie();
}

TEST_F(CpuResourceHandlerFactoryTest, NewNoCpuset) {
  unique_ptr<MockEventFdNotifications> mock_notifications(
      MockEventFdNotifications::NewStrict());

  // Cpuset is not mounted.
  EXPECT_CALL(*mock_cgroup_factory_, IsMounted(CGROUP_CPU))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cgroup_factory_, IsMounted(CGROUP_CPUACCT))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cgroup_factory_, IsMounted(CGROUP_CPUSET))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*mock_cgroup_factory_, OwnsCgroup(_))
      .WillRepeatedly(Return(true));

  StatusOr<ResourceHandlerFactory *> statusor = CpuResourceHandlerFactory::New(
      mock_cgroup_factory_.get(), mock_kernel_.get(), mock_notifications.get());
  ASSERT_OK(statusor);
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  delete statusor.ValueOrDie();
}

TEST_F(CpuResourceHandlerFactoryTest, NewNotMounted) {
  unique_ptr<MockEventFdNotifications> mock_notifications(
      MockEventFdNotifications::NewStrict());

  EXPECT_CALL(*mock_cgroup_factory_, IsMounted(_))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*mock_cgroup_factory_, OwnsCgroup(_))
      .WillRepeatedly(Return(true));

  StatusOr<ResourceHandlerFactory *> statusor = CpuResourceHandlerFactory::New(
      mock_cgroup_factory_.get(), mock_kernel_.get(), mock_notifications.get());
  EXPECT_ERROR_CODE(::util::error::NOT_FOUND, statusor);
}

// Tests for Get().

TEST_F(CpuResourceHandlerFactoryTest, GetSuccess) {
  StrictMockCpuController *mock_cpu_controller = new StrictMockCpuController();
  StrictMockCpusetController *mock_cpuset_controller =
      new StrictMockCpusetController();
  StrictMockCpuAcctController *mock_cpuacct_controller =
      new StrictMockCpuAcctController();
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists(kContainerName))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cpu_controller_factory_, Get(kContainerName))
      .WillRepeatedly(Return(mock_cpu_controller));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Get(kContainerName))
      .WillRepeatedly(Return(mock_cpuacct_controller));
  EXPECT_CALL(*mock_cpuset_controller_factory_, Get(kContainerName))
      .WillRepeatedly(Return(mock_cpuset_controller));

  StatusOr<ResourceHandler *> statusor = CallGetResourceHandler(kContainerName);
  ASSERT_TRUE(statusor.ok());
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  unique_ptr<ResourceHandler> handler(statusor.ValueOrDie());
  EXPECT_EQ(RESOURCE_CPU, handler->type());
  EXPECT_EQ(kContainerName, handler->container_name());
}

TEST_F(CpuResourceHandlerFactoryTest, GetLsTaskInAlloc) {
  const string kFullContainerName = "/alloc/task";
  const string kBatchContainerName = "/batch/alloc/task";
  const string kBaseContainerName = "/task";

  StrictMockCpuController *mock_cpu_controller = new StrictMockCpuController();
  StrictMockCpusetController *mock_cpuset_controller =
      new StrictMockCpusetController();
  StrictMockCpuAcctController *mock_cpuacct_controller =
      new StrictMockCpuAcctController();
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists(kFullContainerName))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists(kBatchContainerName))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists(kBaseContainerName))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cpu_controller_factory_, Get(kBaseContainerName))
      .WillRepeatedly(Return(mock_cpu_controller));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Get(kBaseContainerName))
      .WillRepeatedly(Return(mock_cpuacct_controller));
  EXPECT_CALL(*mock_cpuset_controller_factory_, Get(kBaseContainerName))
      .WillRepeatedly(Return(mock_cpuset_controller));

  StatusOr<ResourceHandler *> statusor =
      CallGetResourceHandler(kFullContainerName);
  ASSERT_OK(statusor);
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  unique_ptr<ResourceHandler> handler(statusor.ValueOrDie());
  EXPECT_EQ(RESOURCE_CPU, handler->type());
  EXPECT_EQ(kFullContainerName, handler->container_name());
}

TEST_F(CpuResourceHandlerFactoryTest, GetLsTaskInAllocWithHierarchicalCpu) {
  const string kFullContainerName = "/alloc/task";
  const string kBaseContainerName = "/task";

  StrictMockCpuController *mock_cpu_controller = new StrictMockCpuController();
  StrictMockCpusetController *mock_cpuset_controller =
      new StrictMockCpusetController();
  StrictMockCpuAcctController *mock_cpuacct_controller =
      new StrictMockCpuAcctController();
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists(kFullContainerName))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cpu_controller_factory_, Get(kFullContainerName))
      .WillRepeatedly(Return(mock_cpu_controller));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Get(kFullContainerName))
      .WillRepeatedly(Return(mock_cpuacct_controller));
  EXPECT_CALL(*mock_cpuset_controller_factory_, Get(kBaseContainerName))
      .WillRepeatedly(Return(mock_cpuset_controller));

  StatusOr<ResourceHandler *> statusor =
      CallGetResourceHandler(kFullContainerName);
  ASSERT_OK(statusor);
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  unique_ptr<ResourceHandler> handler(statusor.ValueOrDie());
  EXPECT_EQ(RESOURCE_CPU, handler->type());
  EXPECT_EQ(kFullContainerName, handler->container_name());
}

TEST_F(CpuResourceHandlerFactoryTest, GetBatchTaskInAlloc) {
  const string kFullContainerName = "/alloc/task";
  const string kBatchContainerName = "/batch/alloc/task";
  const string kBaseContainerName = "/task";
  const string kBatchBaseContainerName = "/batch/task";

  StrictMockCpuController *mock_cpu_controller = new StrictMockCpuController();
  StrictMockCpusetController *mock_cpuset_controller =
      new StrictMockCpusetController();
  StrictMockCpuAcctController *mock_cpuacct_controller =
      new StrictMockCpuAcctController();
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists(kFullContainerName))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists(kBatchContainerName))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists(kBaseContainerName))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists(kBatchBaseContainerName))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cpu_controller_factory_, Get(kBatchBaseContainerName))
      .WillRepeatedly(Return(mock_cpu_controller));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Get(kBatchBaseContainerName))
      .WillRepeatedly(Return(mock_cpuacct_controller));
  EXPECT_CALL(*mock_cpuset_controller_factory_, Get(kBaseContainerName))
      .WillRepeatedly(Return(mock_cpuset_controller));

  StatusOr<ResourceHandler *> statusor =
      CallGetResourceHandler(kFullContainerName);
  ASSERT_OK(statusor);
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  unique_ptr<ResourceHandler> handler(statusor.ValueOrDie());
  EXPECT_EQ(RESOURCE_CPU, handler->type());
  EXPECT_EQ(kFullContainerName, handler->container_name());
}

TEST_F(CpuResourceHandlerFactoryTest, GetLsTaskSubcontainer) {
  const string kFullContainerName = "/task/sub";
  const string kBaseContainerName = "/sub";

  StrictMockCpuController *mock_cpu_controller = new StrictMockCpuController();
  StrictMockCpusetController *mock_cpuset_controller =
      new StrictMockCpusetController();
  StrictMockCpuAcctController *mock_cpuacct_controller =
      new StrictMockCpuAcctController();
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists(kFullContainerName))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cpu_controller_factory_, Get(kFullContainerName))
      .WillRepeatedly(Return(mock_cpu_controller));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Get(kFullContainerName))
      .WillRepeatedly(Return(mock_cpuacct_controller));
  EXPECT_CALL(*mock_cpuset_controller_factory_, Get(kBaseContainerName))
      .WillRepeatedly(Return(mock_cpuset_controller));

  StatusOr<ResourceHandler *> statusor =
      CallGetResourceHandler(kFullContainerName);
  ASSERT_OK(statusor);
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  unique_ptr<ResourceHandler> handler(statusor.ValueOrDie());
  EXPECT_EQ(RESOURCE_CPU, handler->type());
  EXPECT_EQ(kFullContainerName, handler->container_name());
}

TEST_F(CpuResourceHandlerFactoryTest, GetBatchTaskSubcontainer) {
  const string kFullContainerName = "/task/sub";
  const string kBatchContainerName = "/batch/task/sub";
  const string kBaseContainerName = "/sub";

  StrictMockCpuController *mock_cpu_controller = new StrictMockCpuController();
  StrictMockCpusetController *mock_cpuset_controller =
      new StrictMockCpusetController();
  StrictMockCpuAcctController *mock_cpuacct_controller =
      new StrictMockCpuAcctController();
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists(kFullContainerName))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists(kBatchContainerName))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cpu_controller_factory_, Get(kBatchContainerName))
      .WillRepeatedly(Return(mock_cpu_controller));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Get(kBatchContainerName))
      .WillRepeatedly(Return(mock_cpuacct_controller));
  EXPECT_CALL(*mock_cpuset_controller_factory_, Get(kBaseContainerName))
      .WillRepeatedly(Return(mock_cpuset_controller));

  StatusOr<ResourceHandler *> statusor =
      CallGetResourceHandler(kFullContainerName);
  ASSERT_OK(statusor);
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  unique_ptr<ResourceHandler> handler(statusor.ValueOrDie());
  EXPECT_EQ(RESOURCE_CPU, handler->type());
  EXPECT_EQ(kFullContainerName, handler->container_name());
}

TEST_F(CpuResourceHandlerFactoryTest,
       GetLsTaskInAllocSubcontainerWithHierarchicalCpu) {
  const string kFullContainerName = "/alloc/task/sub";
  const string kBaseContainerName = "/sub";

  StrictMockCpuController *mock_cpu_controller = new StrictMockCpuController();
  StrictMockCpusetController *mock_cpuset_controller =
      new StrictMockCpusetController();
  StrictMockCpuAcctController *mock_cpuacct_controller =
      new StrictMockCpuAcctController();
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists(kFullContainerName))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cpu_controller_factory_, Get(kFullContainerName))
      .WillRepeatedly(Return(mock_cpu_controller));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Get(kFullContainerName))
      .WillRepeatedly(Return(mock_cpuacct_controller));
  EXPECT_CALL(*mock_cpuset_controller_factory_, Get(kBaseContainerName))
      .WillRepeatedly(Return(mock_cpuset_controller));

  StatusOr<ResourceHandler *> statusor =
      CallGetResourceHandler(kFullContainerName);
  ASSERT_OK(statusor);
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  unique_ptr<ResourceHandler> handler(statusor.ValueOrDie());
  EXPECT_EQ(RESOURCE_CPU, handler->type());
  EXPECT_EQ(kFullContainerName, handler->container_name());
}

TEST_F(CpuResourceHandlerFactoryTest, GetLsTaskInAllocSubcontainer) {
  const string kFullContainerName = "/alloc/task/sub";
  const string kBatchContainerName = "/batch/alloc/task/sub";
  const string kBaseLsContainerName = "/task/sub";
  const string kBaseContainerName = "/sub";

  StrictMockCpuController *mock_cpu_controller = new StrictMockCpuController();
  StrictMockCpusetController *mock_cpuset_controller =
      new StrictMockCpusetController();
  StrictMockCpuAcctController *mock_cpuacct_controller =
      new StrictMockCpuAcctController();
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists(kFullContainerName))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists(kBatchContainerName))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists(kBaseLsContainerName))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cpu_controller_factory_, Get(kBaseLsContainerName))
      .WillRepeatedly(Return(mock_cpu_controller));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Get(kBaseLsContainerName))
      .WillRepeatedly(Return(mock_cpuacct_controller));
  EXPECT_CALL(*mock_cpuset_controller_factory_, Get(kBaseContainerName))
      .WillRepeatedly(Return(mock_cpuset_controller));

  StatusOr<ResourceHandler *> statusor =
      CallGetResourceHandler(kFullContainerName);
  ASSERT_OK(statusor);
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  unique_ptr<ResourceHandler> handler(statusor.ValueOrDie());
  EXPECT_EQ(RESOURCE_CPU, handler->type());
  EXPECT_EQ(kFullContainerName, handler->container_name());
}

TEST_F(CpuResourceHandlerFactoryTest, GetBatchTaskInAllocSubcontainer) {
  const string kFullContainerName = "/alloc/task/sub";
  const string kBatchContainerName = "/batch/alloc/task/sub";
  const string kBaseLsContainerName = "/task/sub";
  const string kBatchBaseContainerName = "/batch/task/sub";
  const string kBaseContainerName = "/sub";

  StrictMockCpuController *mock_cpu_controller = new StrictMockCpuController();
  StrictMockCpusetController *mock_cpuset_controller =
      new StrictMockCpusetController();
  StrictMockCpuAcctController *mock_cpuacct_controller =
      new StrictMockCpuAcctController();
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists(kFullContainerName))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists(kBatchContainerName))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists(kBaseLsContainerName))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists(kBatchBaseContainerName))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cpu_controller_factory_, Get(kBatchBaseContainerName))
      .WillRepeatedly(Return(mock_cpu_controller));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Get(kBatchBaseContainerName))
      .WillRepeatedly(Return(mock_cpuacct_controller));
  EXPECT_CALL(*mock_cpuset_controller_factory_, Get(kBaseContainerName))
      .WillRepeatedly(Return(mock_cpuset_controller));

  StatusOr<ResourceHandler *> statusor =
      CallGetResourceHandler(kFullContainerName);
  ASSERT_OK(statusor);
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  unique_ptr<ResourceHandler> handler(statusor.ValueOrDie());
  EXPECT_EQ(RESOURCE_CPU, handler->type());
  EXPECT_EQ(kFullContainerName, handler->container_name());
}

TEST_F(CpuResourceHandlerFactoryTest, GetNonExistentTopLevelContainer) {
  const string kFullContainerName = "/task";
  const string kBatchContainerName = "/batch/task";

  EXPECT_CALL(*mock_cpu_controller_factory_, Exists(kFullContainerName))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists(kBatchContainerName))
      .WillRepeatedly(Return(false));

  EXPECT_ERROR_CODE(NOT_FOUND, CallGetResourceHandler(kFullContainerName));
}

TEST_F(CpuResourceHandlerFactoryTest, GetNonExistentContainerTwoLayers) {
  const string kFullContainerName = "/alloc/task";
  const string kBatchContainerName = "/batch/alloc/task";
  const string kBaseLsContainerName = "/task";
  const string kBatchBaseContainerName = "/batch/task";

  EXPECT_CALL(*mock_cpu_controller_factory_, Exists(kFullContainerName))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists(kBatchContainerName))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists(kBaseLsContainerName))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists(kBatchBaseContainerName))
      .WillRepeatedly(Return(false));

  EXPECT_ERROR_CODE(NOT_FOUND, CallGetResourceHandler(kFullContainerName));
}

TEST_F(CpuResourceHandlerFactoryTest, GetNonExistentContainerThreeLayers) {
  const string kFullContainerName = "/alloc/task/sub";
  const string kBatchContainerName = "/batch/alloc/task/sub";
  const string kBaseLsContainerName = "/task/sub";
  const string kBatchBaseContainerName = "/batch/task/sub";

  EXPECT_CALL(*mock_cpu_controller_factory_, Exists(kFullContainerName))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists(kBatchContainerName))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists(kBaseLsContainerName))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists(kBatchBaseContainerName))
      .WillRepeatedly(Return(false));

  EXPECT_ERROR_CODE(NOT_FOUND, CallGetResourceHandler(kFullContainerName));
}

TEST_F(CpuResourceHandlerFactoryTest, GetSuccessNoCpuset) {
  SetUpFactory(false);

  StrictMockCpuController *mock_cpu_controller = new StrictMockCpuController();
  StrictMockCpuAcctController *mock_cpuacct_controller =
      new StrictMockCpuAcctController();
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists(kContainerName))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cpu_controller_factory_, Get(kContainerName))
      .WillRepeatedly(Return(mock_cpu_controller));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Get(kContainerName))
      .WillRepeatedly(Return(mock_cpuacct_controller));

  StatusOr<ResourceHandler *> statusor = CallGetResourceHandler(kContainerName);
  ASSERT_TRUE(statusor.ok());
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  unique_ptr<ResourceHandler> handler(statusor.ValueOrDie());
  EXPECT_EQ(RESOURCE_CPU, handler->type());
  EXPECT_EQ(kContainerName, handler->container_name());
}

TEST_F(CpuResourceHandlerFactoryTest, GetBatchContainer) {
  StrictMockCpuController *mock_cpu_controller = new StrictMockCpuController();
  StrictMockCpusetController *mock_cpuset_controller =
      new StrictMockCpusetController();
  StrictMockCpuAcctController *mock_cpuacct_controller =
      new StrictMockCpuAcctController();
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists(kContainerName))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists(kBatchHierarchyPath))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cpu_controller_factory_, Get(kBatchHierarchyPath))
      .WillRepeatedly(Return(mock_cpu_controller));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Get(kBatchHierarchyPath))
      .WillRepeatedly(Return(mock_cpuacct_controller));
  EXPECT_CALL(*mock_cpuset_controller_factory_, Get(kContainerName))
      .WillRepeatedly(Return(mock_cpuset_controller));

  StatusOr<ResourceHandler *> statusor = CallGetResourceHandler(kContainerName);
  ASSERT_TRUE(statusor.ok());
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  unique_ptr<ResourceHandler> handler(statusor.ValueOrDie());
  EXPECT_EQ(RESOURCE_CPU, handler->type());
  EXPECT_EQ(kContainerName, handler->container_name());
}

TEST_F(CpuResourceHandlerFactoryTest, GetUnknownContainer) {
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists(kContainerName))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists(kBatchHierarchyPath))
      .WillRepeatedly(Return(false));

  EXPECT_ERROR_CODE(NOT_FOUND, CallGetResourceHandler(kContainerName));
}

TEST_F(CpuResourceHandlerFactoryTest, GetCpuControllerFails) {
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists(kContainerName))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cpu_controller_factory_, Get(kContainerName))
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED, CallGetResourceHandler(kContainerName).status());
}

TEST_F(CpuResourceHandlerFactoryTest, GetCpuAcctControllerFails) {
  StrictMockCpuController *mock_cpu_controller = new StrictMockCpuController();
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists(kContainerName))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cpu_controller_factory_, Get(kContainerName))
      .WillRepeatedly(Return(mock_cpu_controller));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Get(kContainerName))
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED, CallGetResourceHandler(kContainerName).status());
}

TEST_F(CpuResourceHandlerFactoryTest, GetCpusetControllerFails) {
  StrictMockCpuController *mock_cpu_controller = new StrictMockCpuController();
  StrictMockCpuAcctController *mock_cpuacct_controller =
      new StrictMockCpuAcctController();
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists(kContainerName))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cpu_controller_factory_, Get(kContainerName))
      .WillRepeatedly(Return(mock_cpu_controller));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Get(kContainerName))
      .WillRepeatedly(Return(mock_cpuacct_controller));
  EXPECT_CALL(*mock_cpuset_controller_factory_, Get(kContainerName))
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED, CallGetResourceHandler(kContainerName).status());
}

// Tests for Create().

TEST_F(CpuResourceHandlerFactoryTest,
       CreateResourceHandlerSucceedsTopLevelTask) {
  ContainerSpec spec;
  spec.mutable_cpu()->set_scheduling_latency(PRIORITY);

  StrictMockCpuController *mock_cpu_controller = new StrictMockCpuController();
  StrictMockCpusetController *mock_cpuset_controller =
      new StrictMockCpusetController();
  StrictMockCpuAcctController *mock_cpuacct_controller =
      new StrictMockCpuAcctController();
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists("/"))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cpu_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_cpu_controller));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_cpuacct_controller));
  EXPECT_CALL(*mock_cpuset_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_cpuset_controller));

  StatusOr<ResourceHandler *> statusor =
      CallCreateResourceHandler(kContainerName, spec);
  ASSERT_TRUE(statusor.ok());
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  unique_ptr<ResourceHandler> handler(statusor.ValueOrDie());
  EXPECT_EQ(RESOURCE_CPU, handler->type());
  EXPECT_EQ(kContainerName, handler->container_name());
}

TEST_F(CpuResourceHandlerFactoryTest,
       CreateResourceHandlerSucceedsTopLevelTaskNoCpuset) {
  SetUpFactory(false);

  ContainerSpec spec;
  spec.mutable_cpu()->set_scheduling_latency(PRIORITY);

  StrictMockCpuController *mock_cpu_controller = new StrictMockCpuController();
  StrictMockCpuAcctController *mock_cpuacct_controller =
      new StrictMockCpuAcctController();
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists("/"))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cpu_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_cpu_controller));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_cpuacct_controller));

  StatusOr<ResourceHandler *> statusor =
      CallCreateResourceHandler(kContainerName, spec);
  ASSERT_TRUE(statusor.ok());
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  unique_ptr<ResourceHandler> handler(statusor.ValueOrDie());
  EXPECT_EQ(RESOURCE_CPU, handler->type());
  EXPECT_EQ(kContainerName, handler->container_name());
}

TEST_F(CpuResourceHandlerFactoryTest,
       CreateResourceHandlerSucceedsTopLevelDefaultTask) {
  ContainerSpec spec;

  StrictMockCpuController *mock_cpu_controller = new StrictMockCpuController();
  StrictMockCpusetController *mock_cpuset_controller =
      new StrictMockCpusetController();
  StrictMockCpuAcctController *mock_cpuacct_controller =
      new StrictMockCpuAcctController();
  // The default latency of PRIORITY corresponds to a top-level task.
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists("/"))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cpu_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_cpu_controller));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_cpuacct_controller));
  EXPECT_CALL(*mock_cpuset_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_cpuset_controller));

  StatusOr<ResourceHandler *> statusor =
      CallCreateResourceHandler(kContainerName, spec);
  ASSERT_TRUE(statusor.ok());
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  unique_ptr<ResourceHandler> handler(statusor.ValueOrDie());
  EXPECT_EQ(RESOURCE_CPU, handler->type());
  EXPECT_EQ(kContainerName, handler->container_name());
}

TEST_F(CpuResourceHandlerFactoryTest,
       CreateResourceHandlerSucceedsTopLevelBatchTask) {
  ContainerSpec spec;
  spec.mutable_cpu()->set_scheduling_latency(BEST_EFFORT);

  StrictMockCpuController *mock_cpu_controller = new StrictMockCpuController();
  StrictMockCpusetController *mock_cpuset_controller =
      new StrictMockCpusetController();
  StrictMockCpuAcctController *mock_cpuacct_controller =
      new StrictMockCpuAcctController();
  const string kBatchContainerName = kBatchHierarchyPath;
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists("/"))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cpu_controller_factory_, Create(kBatchContainerName))
      .WillRepeatedly(Return(mock_cpu_controller));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Create(kBatchContainerName))
      .WillRepeatedly(Return(mock_cpuacct_controller));
  EXPECT_CALL(*mock_cpuset_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_cpuset_controller));

  StatusOr<ResourceHandler *> statusor =
      CallCreateResourceHandler(kContainerName, spec);
  ASSERT_TRUE(statusor.ok());
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  unique_ptr<ResourceHandler> handler(statusor.ValueOrDie());
  EXPECT_EQ(RESOURCE_CPU, handler->type());
  EXPECT_EQ(kContainerName, handler->container_name());
}

TEST_F(CpuResourceHandlerFactoryTest,
       CreateResourceHandlerSucceedsLsTaskUnderAlloc) {
  ContainerSpec spec;
  spec.mutable_cpu()->set_scheduling_latency(PRIORITY);

  StrictMockCpuController *mock_cpu_controller = new StrictMockCpuController();
  StrictMockCpusetController *mock_cpuset_controller =
      new StrictMockCpusetController();
  StrictMockCpuAcctController *mock_cpuacct_controller =
      new StrictMockCpuAcctController();
  const char kContainerFullName[] = "/alloc/test";
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists("/alloc"))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cpu_controller_factory_, Create(kContainerFullName))
      .WillRepeatedly(Return(mock_cpu_controller));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Create(kContainerFullName))
      .WillRepeatedly(Return(mock_cpuacct_controller));
  // cpuset is flat.
  EXPECT_CALL(*mock_cpuset_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_cpuset_controller));

  StatusOr<ResourceHandler *> statusor =
      CallCreateResourceHandler(kContainerFullName, spec);
  ASSERT_TRUE(statusor.ok());
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  unique_ptr<ResourceHandler> handler(statusor.ValueOrDie());
  EXPECT_EQ(RESOURCE_CPU, handler->type());
  EXPECT_EQ(kContainerFullName, handler->container_name());
}

TEST_F(CpuResourceHandlerFactoryTest,
       CreateResourceHandlerSucceedsLsTaskUnderAllocNoCpuset) {
  SetUpFactory(false);

  ContainerSpec spec;
  spec.mutable_cpu()->set_scheduling_latency(PRIORITY);

  StrictMockCpuController *mock_cpu_controller = new StrictMockCpuController();
  StrictMockCpuAcctController *mock_cpuacct_controller =
      new StrictMockCpuAcctController();
  const char kContainerFullName[] = "/alloc/test";
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists("/alloc"))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cpu_controller_factory_, Create(kContainerFullName))
      .WillRepeatedly(Return(mock_cpu_controller));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Create(kContainerFullName))
      .WillRepeatedly(Return(mock_cpuacct_controller));

  StatusOr<ResourceHandler *> statusor =
      CallCreateResourceHandler(kContainerFullName, spec);
  ASSERT_TRUE(statusor.ok());
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  unique_ptr<ResourceHandler> handler(statusor.ValueOrDie());
  EXPECT_EQ(RESOURCE_CPU, handler->type());
  EXPECT_EQ(kContainerFullName, handler->container_name());
}

TEST_F(CpuResourceHandlerFactoryTest,
       CreateResourceHandlerSucceedsTaskUnderBatchAlloc) {
  ContainerSpec spec;
  spec.mutable_cpu()->set_scheduling_latency(BEST_EFFORT);

  StrictMockCpuController *mock_cpu_controller = new StrictMockCpuController();
  StrictMockCpusetController *mock_cpuset_controller =
      new StrictMockCpusetController();
  StrictMockCpuAcctController *mock_cpuacct_controller =
      new StrictMockCpuAcctController();
  const char kContainerFullName[] = "/alloc/test";
  const char kExpectedName[] = "/batch/alloc/test";
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists("/alloc"))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists("/batch/alloc"))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cpu_controller_factory_, Create(kExpectedName))
      .WillRepeatedly(Return(mock_cpu_controller));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Create(kExpectedName))
      .WillRepeatedly(Return(mock_cpuacct_controller));
  // cpuset is flat.
  EXPECT_CALL(*mock_cpuset_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_cpuset_controller));

  StatusOr<ResourceHandler *> statusor =
      CallCreateResourceHandler(kContainerFullName, spec);
  ASSERT_TRUE(statusor.ok());
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  unique_ptr<ResourceHandler> handler(statusor.ValueOrDie());
  EXPECT_EQ(RESOURCE_CPU, handler->type());
  EXPECT_EQ(kContainerFullName, handler->container_name());
}


TEST_F(CpuResourceHandlerFactoryTest, CreateCpuControllerFails) {
  ContainerSpec spec;
  spec.mutable_cpu()->set_scheduling_latency(PRIORITY);

  EXPECT_CALL(*mock_cpu_controller_factory_, Exists("/"))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cpu_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED,
            CallCreateResourceHandler(kContainerName, spec).status());
}

TEST_F(CpuResourceHandlerFactoryTest, CreateCpuAcctControllerFails) {
  ContainerSpec spec;
  spec.mutable_cpu()->set_scheduling_latency(PRIORITY);

  StrictMockCpuController *mock_cpu_controller = new StrictMockCpuController();
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists("/"))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cpu_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_cpu_controller));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED,
            CallCreateResourceHandler(kContainerName, spec).status());
}

TEST_F(CpuResourceHandlerFactoryTest, CreateCpusetControllerFails) {
  ContainerSpec spec;
  spec.mutable_cpu()->set_scheduling_latency(PRIORITY);

  StrictMockCpuController *mock_cpu_controller = new StrictMockCpuController();
  StrictMockCpuAcctController *mock_cpuacct_controller =
      new StrictMockCpuAcctController();
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists("/"))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cpu_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_cpu_controller));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_cpuacct_controller));
  EXPECT_CALL(*mock_cpuset_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED,
            CallCreateResourceHandler(kContainerName, spec).status());
}

TEST_F(CpuResourceHandlerFactoryTest, CreateResourceHandlerFailsMissingParent) {
  ContainerSpec spec;

  StrictMockCpuController *mock_cpu_controller = new StrictMockCpuController();
  StrictMockCpusetController *mock_cpuset_controller =
      new StrictMockCpusetController();
  StrictMockCpuAcctController *mock_cpuacct_controller =
      new StrictMockCpuAcctController();
  const char kContainerFullName[] = "/alloc/test";
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists("/alloc"))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists("/batch/alloc"))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*mock_cpu_controller_factory_, Create(kContainerFullName))
      .WillRepeatedly(Return(mock_cpu_controller));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Create(kContainerFullName))
      .WillRepeatedly(Return(mock_cpuacct_controller));
  EXPECT_CALL(*mock_cpuset_controller_factory_, Create(kContainerFullName))
      .WillRepeatedly(Return(mock_cpuset_controller));

  StatusOr<ResourceHandler *> statusor =
            CallCreateResourceHandler(kContainerFullName, spec);
  EXPECT_NOT_OK(statusor);
  EXPECT_EQ(NOT_FOUND, statusor.status().error_code());
}

TEST_F(CpuResourceHandlerFactoryTest, CreateSucceeds) {
  ContainerSpec spec;

  StrictMockCpuController *mock_cpu_controller = new StrictMockCpuController();
  StrictMockCpusetController *mock_cpuset_controller =
      new StrictMockCpusetController();
  StrictMockCpuAcctController *mock_cpuacct_controller =
      new StrictMockCpuAcctController();
  // The default latency of PRIORITY corresponds to a top-level task.
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists("/"))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cpu_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_cpu_controller));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_cpuacct_controller));
  EXPECT_CALL(*mock_cpuset_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_cpuset_controller));

  // Latency should be set to PRIORITY.
  EXPECT_CALL(*mock_cpu_controller, SetLatency(PRIORITY))
      .WillRepeatedly(Return(Status::OK));
  EXPECT_CALL(*mock_cpuacct_controller, SetupHistograms())
      .WillRepeatedly(Return(Status::OK));

  StatusOr<ResourceHandler *> statusor = CallCreate(kContainerName, spec);
  ASSERT_TRUE(statusor.ok());
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  unique_ptr<ResourceHandler> handler(statusor.ValueOrDie());
  EXPECT_EQ(RESOURCE_CPU, handler->type());
  EXPECT_EQ(kContainerName, handler->container_name());
}

TEST_F(CpuResourceHandlerFactoryTest, CreateCpuCreateFails) {
  ContainerSpec spec;

  // The default latency of PRIORITY corresponds to a top-level taks.
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists("/"))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cpu_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED, CallCreate(kContainerName, spec).status());
}

TEST_F(CpuResourceHandlerFactoryTest, CreateSetLatencyNotFound) {
  ContainerSpec spec;
  spec.mutable_cpu()->set_scheduling_latency(PRIORITY);

  StrictMockCpuController *mock_cpu_controller = new StrictMockCpuController();
  StrictMockCpusetController *mock_cpuset_controller =
      new StrictMockCpusetController();
  StrictMockCpuAcctController *mock_cpuacct_controller =
      new StrictMockCpuAcctController();
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists("/"))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cpu_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_cpu_controller));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_cpuacct_controller));
  EXPECT_CALL(*mock_cpuset_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_cpuset_controller));

  EXPECT_CALL(*mock_cpu_controller, SetLatency(PRIORITY))
      .WillRepeatedly(Return(Status(NOT_FOUND, "")));
  EXPECT_CALL(*mock_cpuacct_controller, SetupHistograms())
      .WillRepeatedly(Return(Status::OK));

  EXPECT_CALL(*mock_cpu_controller, GetLatency())
      .WillRepeatedly(Return(PRIORITY));

  // We ignore SetLatency() when it is NOT_FOUND.
  StatusOr<ResourceHandler *> statusor = CallCreate(kContainerName, spec);
  ASSERT_TRUE(statusor.ok());
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  unique_ptr<ResourceHandler> handler(statusor.ValueOrDie());
  EXPECT_EQ(RESOURCE_CPU, handler->type());
  EXPECT_EQ(kContainerName, handler->container_name());
}

TEST_F(CpuResourceHandlerFactoryTest, CreateSetupHistogramsNotFound) {
  ContainerSpec spec;

  StrictMockCpuController *mock_cpu_controller = new StrictMockCpuController();
  StrictMockCpusetController *mock_cpuset_controller =
      new StrictMockCpusetController();
  StrictMockCpuAcctController *mock_cpuacct_controller =
      new StrictMockCpuAcctController();
  // The default latency of PRIORITY corresponds to a top-level task.
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists("/"))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cpu_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_cpu_controller));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_cpuacct_controller));
  EXPECT_CALL(*mock_cpuset_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_cpuset_controller));

  // Latency should be set to PRIORITY.
  EXPECT_CALL(*mock_cpu_controller, SetLatency(PRIORITY))
      .WillRepeatedly(Return(Status::OK));
  EXPECT_CALL(*mock_cpuacct_controller, SetupHistograms())
      .WillRepeatedly(Return(Status(NOT_FOUND, "")));

  // We ignore SetupHistograms() not being supported.
  StatusOr<ResourceHandler *> statusor = CallCreate(kContainerName, spec);
  ASSERT_TRUE(statusor.ok());
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  unique_ptr<ResourceHandler> handler(statusor.ValueOrDie());
  EXPECT_EQ(RESOURCE_CPU, handler->type());
  EXPECT_EQ(kContainerName, handler->container_name());
}

TEST_F(CpuResourceHandlerFactoryTest, CreateSetLatencyFails) {
  ContainerSpec spec;
  CpuSpec *cpu_spec = spec.mutable_cpu();
  cpu_spec->set_scheduling_latency(PRIORITY);

  StrictMockCpuController *mock_cpu_controller = new StrictMockCpuController();
  StrictMockCpusetController *mock_cpuset_controller =
      new StrictMockCpusetController();
  StrictMockCpuAcctController *mock_cpuacct_controller =
      new StrictMockCpuAcctController();
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists("/"))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cpu_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_cpu_controller));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_cpuacct_controller));
  EXPECT_CALL(*mock_cpuset_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_cpuset_controller));

  // Fail to set up latency.
  EXPECT_CALL(*mock_cpu_controller, SetLatency(PRIORITY))
      .WillRepeatedly(Return(Status::CANCELLED));
  EXPECT_CALL(*mock_cpuacct_controller, SetupHistograms())
      .WillRepeatedly(Return(Status::OK));

  EXPECT_EQ(Status::CANCELLED, CallCreate(kContainerName, spec).status());
}

TEST_F(CpuResourceHandlerFactoryTest, CreateSettingHistogramsFails) {
  ContainerSpec spec;
  spec.mutable_cpu()->set_scheduling_latency(PRIORITY);

  StrictMockCpuController *mock_cpu_controller = new StrictMockCpuController();
  StrictMockCpusetController *mock_cpuset_controller =
      new StrictMockCpusetController();
  StrictMockCpuAcctController *mock_cpuacct_controller =
      new StrictMockCpuAcctController();
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists("/"))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cpu_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_cpu_controller));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_cpuacct_controller));
  EXPECT_CALL(*mock_cpuset_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_cpuset_controller));

  EXPECT_CALL(*mock_cpu_controller, SetLatency(PRIORITY))
      .WillRepeatedly(Return(Status::OK));

  // Fail to set up histograms.
  EXPECT_CALL(*mock_cpuacct_controller, SetupHistograms())
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED, CallCreate(kContainerName, spec).status());
}

// Tests for InitMachine().

TEST_F(CpuResourceHandlerFactoryTest, InitMachineSuccess) {
  InitSpec spec;

  StrictMockCpuController *mock_cpu_controller = new StrictMockCpuController();
  StrictMockCpusetController *mock_cpuset_controller =
      new StrictMockCpusetController();
  StrictMockCpuAcctController *mock_cpuacct_controller =
      new StrictMockCpuAcctController();
  EXPECT_CALL(*mock_cpu_controller_factory_, Create("/batch"))
      .WillOnce(Return(mock_cpu_controller));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Create("/batch"))
      .WillOnce(Return(mock_cpuacct_controller));
  EXPECT_CALL(*mock_cpuset_controller_factory_, Get("/"))
      .WillOnce(Return(mock_cpuset_controller));

  EXPECT_CALL(*mock_cpu_controller, SetMilliCpus(0))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_cpuacct_controller, SetupHistograms())
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_cpuset_controller, EnableCloneChildren())
      .WillOnce(Return(Status::OK));

  EXPECT_OK(CallInitMachine(spec));
}

TEST_F(CpuResourceHandlerFactoryTest, InitMachineSuccessNoCpuset) {
  SetUpFactory(false);
  InitSpec spec;

  StrictMockCpuController *mock_cpu_controller = new StrictMockCpuController();
  StrictMockCpuAcctController *mock_cpuacct_controller =
      new StrictMockCpuAcctController();
  EXPECT_CALL(*mock_cpu_controller_factory_, Create("/batch"))
      .WillOnce(Return(mock_cpu_controller));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Create("/batch"))
      .WillOnce(Return(mock_cpuacct_controller));

  EXPECT_CALL(*mock_cpu_controller, SetMilliCpus(0))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_cpuacct_controller, SetupHistograms())
      .WillOnce(Return(Status::OK));

  EXPECT_OK(CallInitMachine(spec));
}

TEST_F(CpuResourceHandlerFactoryTest, InitMachineCpuCreateFails) {
  InitSpec spec;

  EXPECT_CALL(*mock_cpu_controller_factory_, Create("/batch"))
      .WillOnce(Return(Status::CANCELLED));

  EXPECT_NOT_OK(CallInitMachine(spec));
}

TEST_F(CpuResourceHandlerFactoryTest, InitMachineCpuAcctCreateFails) {
  InitSpec spec;

  StrictMockCpuController *mock_cpu_controller = new StrictMockCpuController();
  EXPECT_CALL(*mock_cpu_controller_factory_, Create("/batch"))
      .WillOnce(Return(mock_cpu_controller));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Create("/batch"))
      .WillOnce(Return(Status::CANCELLED));

  EXPECT_CALL(*mock_cpu_controller, SetMilliCpus(0))
      .WillRepeatedly(Return(Status::OK));

  EXPECT_NOT_OK(CallInitMachine(spec));
}

TEST_F(CpuResourceHandlerFactoryTest, InitMachineCpusetGetFails) {
  InitSpec spec;

  StrictMockCpuController *mock_cpu_controller = new StrictMockCpuController();
  StrictMockCpuAcctController *mock_cpuacct_controller =
      new StrictMockCpuAcctController();
  EXPECT_CALL(*mock_cpu_controller_factory_, Create("/batch"))
      .WillOnce(Return(mock_cpu_controller));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Create("/batch"))
      .WillOnce(Return(mock_cpuacct_controller));
  EXPECT_CALL(*mock_cpuset_controller_factory_, Get("/"))
      .WillOnce(Return(Status::CANCELLED));

  EXPECT_CALL(*mock_cpu_controller, SetMilliCpus(0))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_cpuacct_controller, SetupHistograms())
      .WillOnce(Return(Status::OK));

  EXPECT_EQ(Status::CANCELLED, CallInitMachine(spec));
}

TEST_F(CpuResourceHandlerFactoryTest, InitMachineAlreadyInitializedSuccess) {
  InitSpec spec;

  StrictMockCpuController *mock_cpu_controller = new StrictMockCpuController();
  StrictMockCpusetController *mock_cpuset_controller =
      new StrictMockCpusetController();
  StrictMockCpuAcctController *mock_cpuacct_controller =
      new StrictMockCpuAcctController();
  EXPECT_CALL(*mock_cpu_controller_factory_, Create("/batch"))
      .WillRepeatedly(Return(Status(::util::error::ALREADY_EXISTS, "")));
  EXPECT_CALL(*mock_cpu_controller_factory_, Get("/batch"))
      .WillOnce(Return(mock_cpu_controller));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Create("/batch"))
      .WillRepeatedly(Return(Status(::util::error::ALREADY_EXISTS, "")));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Get("/batch"))
      .WillOnce(Return(mock_cpuacct_controller));
  EXPECT_CALL(*mock_cpuset_controller_factory_, Get("/"))
      .WillOnce(Return(mock_cpuset_controller));

  EXPECT_CALL(*mock_cpu_controller, SetMilliCpus(0))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_cpuacct_controller, SetupHistograms())
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_cpuset_controller, EnableCloneChildren())
      .WillOnce(Return(Status::OK));

  EXPECT_OK(CallInitMachine(spec));
}

TEST_F(CpuResourceHandlerFactoryTest,
       InitMachineAlreadyInitializedSuccessNoCpuset) {
  SetUpFactory(false);
  InitSpec spec;

  StrictMockCpuController *mock_cpu_controller = new StrictMockCpuController();
  StrictMockCpuAcctController *mock_cpuacct_controller =
      new StrictMockCpuAcctController();
  EXPECT_CALL(*mock_cpu_controller_factory_, Create("/batch"))
      .WillRepeatedly(Return(Status(::util::error::ALREADY_EXISTS, "")));
  EXPECT_CALL(*mock_cpu_controller_factory_, Get("/batch"))
      .WillOnce(Return(mock_cpu_controller));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Create("/batch"))
      .WillRepeatedly(Return(Status(::util::error::ALREADY_EXISTS, "")));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Get("/batch"))
      .WillOnce(Return(mock_cpuacct_controller));

  EXPECT_CALL(*mock_cpu_controller, SetMilliCpus(0))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_cpuacct_controller, SetupHistograms())
      .WillOnce(Return(Status::OK));

  EXPECT_OK(CallInitMachine(spec));
}

TEST_F(CpuResourceHandlerFactoryTest,
       InitMachineAlreadyInitializedCpuGetFails) {
  InitSpec spec;

  EXPECT_CALL(*mock_cpu_controller_factory_, Create("/batch"))
      .WillRepeatedly(Return(Status(::util::error::ALREADY_EXISTS, "")));
  EXPECT_CALL(*mock_cpu_controller_factory_, Get("/batch"))
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_NOT_OK(CallInitMachine(spec));
}

TEST_F(CpuResourceHandlerFactoryTest,
       InitMachineAlreadyInitializedCpuAcctGetFails) {
  InitSpec spec;

  StrictMockCpuController *mock_cpu_controller = new StrictMockCpuController();
  EXPECT_CALL(*mock_cpu_controller_factory_, Create("/batch"))
      .WillRepeatedly(Return(Status(::util::error::ALREADY_EXISTS, "")));
  EXPECT_CALL(*mock_cpu_controller_factory_, Get("/batch"))
      .WillOnce(Return(mock_cpu_controller));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Create("/batch"))
      .WillRepeatedly(Return(Status(::util::error::ALREADY_EXISTS, "")));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Get("/batch"))
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_CALL(*mock_cpu_controller, SetMilliCpus(0))
      .WillRepeatedly(Return(Status::OK));

  EXPECT_NOT_OK(CallInitMachine(spec));
}

TEST_F(CpuResourceHandlerFactoryTest, InitMachineSetMilliCpusFails) {
  InitSpec spec;

  StrictMockCpuController *mock_cpu_controller = new StrictMockCpuController();
  StrictMockCpuAcctController *mock_cpuacct_controller =
      new StrictMockCpuAcctController();
  EXPECT_CALL(*mock_cpu_controller_factory_, Create("/batch"))
      .WillOnce(Return(mock_cpu_controller));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Create("/batch"))
      .WillOnce(Return(mock_cpuacct_controller));

  EXPECT_CALL(*mock_cpu_controller, SetMilliCpus(0))
      .WillOnce(Return(Status::CANCELLED));
  EXPECT_CALL(*mock_cpuacct_controller, SetupHistograms())
      .WillRepeatedly(Return(Status::OK));

  EXPECT_NOT_OK(CallInitMachine(spec));
}

TEST_F(CpuResourceHandlerFactoryTest, InitMachineSetupHistogramsFails) {
  InitSpec spec;

  StrictMockCpuController *mock_cpu_controller = new StrictMockCpuController();
  StrictMockCpuAcctController *mock_cpuacct_controller =
      new StrictMockCpuAcctController();
  EXPECT_CALL(*mock_cpu_controller_factory_, Create("/batch"))
      .WillOnce(Return(mock_cpu_controller));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Create("/batch"))
      .WillOnce(Return(mock_cpuacct_controller));

  EXPECT_CALL(*mock_cpu_controller, SetMilliCpus(0))
      .WillRepeatedly(Return(Status::OK));
  EXPECT_CALL(*mock_cpuacct_controller, SetupHistograms())
      .WillOnce(Return(Status::CANCELLED));

  EXPECT_NOT_OK(CallInitMachine(spec));
}

TEST_F(CpuResourceHandlerFactoryTest, InitMachineSetupHistogramsNotFound) {
  InitSpec spec;

  StrictMockCpuController *mock_cpu_controller = new StrictMockCpuController();
  StrictMockCpusetController *mock_cpuset_controller =
      new StrictMockCpusetController();
  StrictMockCpuAcctController *mock_cpuacct_controller =
      new StrictMockCpuAcctController();
  EXPECT_CALL(*mock_cpu_controller_factory_, Create("/batch"))
      .WillOnce(Return(mock_cpu_controller));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Create("/batch"))
      .WillOnce(Return(mock_cpuacct_controller));
  EXPECT_CALL(*mock_cpuset_controller_factory_, Get("/"))
      .WillOnce(Return(mock_cpuset_controller));

  EXPECT_CALL(*mock_cpu_controller, SetMilliCpus(0))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_cpuacct_controller, SetupHistograms())
      .WillOnce(Return(Status(NOT_FOUND, "")));
  EXPECT_CALL(*mock_cpuset_controller, EnableCloneChildren())
      .WillOnce(Return(Status::OK));

  // SetupHistogram() is allowed to be NOT_FOUND.
  EXPECT_OK(CallInitMachine(spec));
}

TEST_F(CpuResourceHandlerFactoryTest, InitMachineEnableCloneChildrenFails) {
  InitSpec spec;

  StrictMockCpuController *mock_cpu_controller = new StrictMockCpuController();
  StrictMockCpusetController *mock_cpuset_controller =
      new StrictMockCpusetController();
  StrictMockCpuAcctController *mock_cpuacct_controller =
      new StrictMockCpuAcctController();
  EXPECT_CALL(*mock_cpu_controller_factory_, Create("/batch"))
      .WillOnce(Return(mock_cpu_controller));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Create("/batch"))
      .WillOnce(Return(mock_cpuacct_controller));
  EXPECT_CALL(*mock_cpuset_controller_factory_, Get("/"))
      .WillOnce(Return(mock_cpuset_controller));

  EXPECT_CALL(*mock_cpu_controller, SetMilliCpus(0))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_cpuacct_controller, SetupHistograms())
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_cpuset_controller, EnableCloneChildren())
      .WillOnce(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED, CallInitMachine(spec));
}

class CpuResourceHandlerTest : public ::testing::Test {
 public:
  CpuResourceHandlerTest()
      : kHistoTypes({SERVE, ONCPU, SLEEP, QUEUE_SELF, QUEUE_OTHER, }),
        kUpdatePolicy({Container::UPDATE_DIFF, Container::UPDATE_REPLACE, }) {}

  virtual void SetUp() {
    SetUpHandler(true);
  }

  virtual void SetUpHandler(bool cpuset_enabled) {
    mock_kernel_.reset(new StrictMock<KernelAPIMock>());
    mock_cpu_controller_ = new StrictMockCpuController();
    mock_cpuacct_controller_ = new StrictMockCpuAcctController();
    mock_cpuset_controller_ = nullptr;

    if (cpuset_enabled) {
      mock_cpuset_controller_ = new StrictMockCpusetController();
    }

    handler_.reset(new CpuResourceHandler(
        kContainerName, mock_kernel_.get(), mock_cpu_controller_,
        mock_cpuacct_controller_,
        cpuset_enabled ? mock_cpuset_controller_ : nullptr));
  }

 protected:
  const vector<CpuHistogramType> kHistoTypes;
  const vector<Container::UpdatePolicy> kUpdatePolicy;

  MockCpuController *mock_cpu_controller_;
  MockCpuAcctController *mock_cpuacct_controller_;
  MockCpusetController *mock_cpuset_controller_;
  unique_ptr<KernelAPIMock> mock_kernel_;
  unique_ptr<CpuResourceHandler> handler_;
};

class CpuStatsTest : public CpuResourceHandlerTest {
 protected:
  void SetUp() override {
    CpuResourceHandlerTest::SetUp();

    expected_throttling_stats_.nr_periods = 100;
    expected_throttling_stats_.nr_throttled = 20;
    expected_throttling_stats_.throttled_time = 123456789;

    // Prepare scheduler histograms.
    for (auto type : kHistoTypes) {
      CpuHistogramData data;
      data.type = type;
      for (int key = 1; key <= 3; ++key) {
        data.buckets[key * 1000] = 100 * key;
      }
      expected_histograms_.push_back(data);
    }
  }

  void ExpectSummaryGets() {
    auto &expected_usage_ = *expected_stats_.mutable_usage();
    EXPECT_CALL(*mock_cpuacct_controller_, GetCpuUsageInNs())
        .WillRepeatedly(Return(expected_total_));
    expected_usage_.set_total(expected_total_);

    EXPECT_CALL(*mock_cpuacct_controller_, GetCpuTime())
        .WillRepeatedly(Return(expected_cpu_time_));
    expected_usage_.set_user(expected_cpu_time_.user.value());
    expected_usage_.set_system(expected_cpu_time_.system.value());

    EXPECT_CALL(*mock_cpuacct_controller_, GetPerCpuUsageInNs())
        .WillRepeatedly(Invoke([this]() {
          return new vector<int64>(expected_per_cpu_);
        }));
    ::std::copy(expected_per_cpu_.begin(),
                expected_per_cpu_.end(),
                RepeatedFieldBackInserter(
                    expected_usage_.mutable_per_cpu()));

    EXPECT_CALL(*mock_cpu_controller_, GetNumRunnable())
        .WillRepeatedly(Return(expected_load_));
    expected_stats_.set_load(expected_load_);
  }

  void ExpectFullGets() {
    ExpectSummaryGets();

    EXPECT_CALL(*mock_cpu_controller_, GetThrottlingStats())
        .WillRepeatedly(Return(expected_throttling_stats_));
    auto *expected_throttling_data = expected_stats_.mutable_throttling_data();
    expected_throttling_data->set_periods(
        expected_throttling_stats_.nr_periods);
    expected_throttling_data->set_throttled_periods(
        expected_throttling_stats_.nr_throttled);
    expected_throttling_data->set_throttled_time(
        expected_throttling_stats_.throttled_time);

    EXPECT_CALL(*mock_cpuacct_controller_, GetSchedulerHistograms())
        .WillRepeatedly(Invoke([this]() {
          auto histograms_ = new ::std::vector<CpuHistogramData *>();
          for (const auto &data : expected_histograms_) {
            histograms_->push_back(new CpuHistogramData(data));
          }
          return histograms_;
        }));
    for (const auto &histogram_data : expected_histograms_) {
      auto *histogram_map = expected_stats_.mutable_histograms()->Add();
      histogram_map->set_type(histogram_data.type);
      for (const auto &bucket : histogram_data.buckets) {
        auto *stat = histogram_map->mutable_stat()->Add();
        stat->set_bucket(bucket.first);
        stat->set_value(bucket.second);
      }
    }
  }

  uint64 expected_total_ = 112233445566;
  int32 expected_load_ = 42;
  CpuTime expected_cpu_time_ = {Nanoseconds(100), Nanoseconds(200)};
  vector<int64> expected_per_cpu_ = {10, 20, 30};
  struct ThrottlingStats expected_throttling_stats_;
  ::std::vector<CpuHistogramData> expected_histograms_;

  CpuStats expected_stats_;
};

TEST_F(CpuStatsTest, StatsSummarySuccess) {
  ExpectSummaryGets();
  Container::StatsType type = Container::STATS_SUMMARY;

  ContainerStats stats;
  EXPECT_OK(handler_->Stats(type, &stats));
  EXPECT_THAT(stats.cpu(), EqualsInitializedProto(expected_stats_));
}

TEST_F(CpuStatsTest, StatsFullSuccess) {
  ExpectFullGets();
  Container::StatsType type = Container::STATS_FULL;

  ContainerStats stats;
  EXPECT_OK(handler_->Stats(type, &stats));
  EXPECT_THAT(stats.cpu(), EqualsInitializedProto(expected_stats_));
}

TEST_F(CpuStatsTest, StatsUsageFails) {
  Container::StatsType type = Container::STATS_FULL;
  ContainerStats stats;

  EXPECT_CALL(*mock_cpuacct_controller_, GetCpuUsageInNs())
      .WillRepeatedly(Return(Status::CANCELLED));
  EXPECT_EQ(Status::CANCELLED, handler_->Stats(type, &stats));
}

TEST_F(CpuStatsTest, StatsLoadFails) {
  Container::StatsType type = Container::STATS_FULL;
  ExpectFullGets();
  EXPECT_CALL(*mock_cpu_controller_, GetNumRunnable())
      .WillRepeatedly(Return(Status::CANCELLED));

  ContainerStats stats;
  EXPECT_EQ(Status::CANCELLED, handler_->Stats(type, &stats));
}

TEST_F(CpuStatsTest, StatsCpuTimeFails) {
  Container::StatsType type = Container::STATS_FULL;
  ExpectFullGets();
  EXPECT_CALL(*mock_cpuacct_controller_, GetCpuTime())
      .WillRepeatedly(Return(Status::CANCELLED));

  ContainerStats stats;
  EXPECT_EQ(Status::CANCELLED, handler_->Stats(type, &stats));
}

TEST_F(CpuStatsTest, StatsPerCpuUsageInNsFails) {
  Container::StatsType type = Container::STATS_FULL;
  ExpectFullGets();
  EXPECT_CALL(*mock_cpuacct_controller_, GetPerCpuUsageInNs())
      .WillRepeatedly(Return(Status::CANCELLED));

  ContainerStats stats;
  EXPECT_EQ(Status::CANCELLED, handler_->Stats(type, &stats));
}

TEST_F(CpuStatsTest, StatsHistogramFails) {
  Container::StatsType type = Container::STATS_FULL;
  ExpectFullGets();
  EXPECT_CALL(*mock_cpuacct_controller_, GetSchedulerHistograms())
      .WillRepeatedly(Return(Status::CANCELLED));

  ContainerStats stats;
  EXPECT_EQ(Status::CANCELLED, handler_->Stats(type, &stats));
}


TEST_F(CpuStatsTest, StatsThrottlingFails) {
  Container::StatsType type = Container::STATS_FULL;
  ExpectFullGets();
  EXPECT_CALL(*mock_cpu_controller_, GetThrottlingStats())
      .WillRepeatedly(Return(Status::CANCELLED));

  ContainerStats stats;
  EXPECT_EQ(Status::CANCELLED, handler_->Stats(type, &stats));
}

TEST_F(CpuStatsTest, StatsUsageNotFound) {
  Container::StatsType type = Container::STATS_FULL;
  ExpectFullGets();
  EXPECT_CALL(*mock_cpuacct_controller_, GetCpuUsageInNs())
      .WillRepeatedly(Return(Status(NOT_FOUND, "")));
  expected_stats_.mutable_usage()->clear_total();

  ContainerStats stats;
  EXPECT_OK(handler_->Stats(type, &stats));
  EXPECT_THAT(stats.cpu(), EqualsInitializedProto(expected_stats_));
}

TEST_F(CpuStatsTest, StatsLoadNotFound) {
  Container::StatsType type = Container::STATS_FULL;
  ExpectFullGets();
  EXPECT_CALL(*mock_cpu_controller_, GetNumRunnable())
      .WillRepeatedly(Return(Status(NOT_FOUND, "")));
  expected_stats_.clear_load();

  ContainerStats stats;
  EXPECT_OK(handler_->Stats(type, &stats));
  EXPECT_THAT(stats.cpu(), EqualsInitializedProto(expected_stats_));
}

TEST_F(CpuStatsTest, StatsCpuTimeNotFound) {
  Container::StatsType type = Container::STATS_FULL;
  ExpectFullGets();
  EXPECT_CALL(*mock_cpuacct_controller_, GetCpuTime())
      .WillRepeatedly(Return(Status(NOT_FOUND, "")));
  expected_stats_.mutable_usage()->clear_user();
  expected_stats_.mutable_usage()->clear_system();

  ContainerStats stats;
  EXPECT_OK(handler_->Stats(type, &stats));
  EXPECT_THAT(stats.cpu(), EqualsInitializedProto(expected_stats_));
}

TEST_F(CpuStatsTest, StatsPerCpuUsageInNsNotFound) {
  Container::StatsType type = Container::STATS_FULL;
  ExpectFullGets();
  EXPECT_CALL(*mock_cpuacct_controller_, GetPerCpuUsageInNs())
      .WillRepeatedly(Return(Status(NOT_FOUND, "")));
  expected_stats_.mutable_usage()->clear_per_cpu();

  ContainerStats stats;
  EXPECT_OK(handler_->Stats(type, &stats));
  EXPECT_THAT(stats.cpu(), EqualsInitializedProto(expected_stats_));
}

TEST_F(CpuStatsTest, StatsHistogramNotFound) {
  Container::StatsType type = Container::STATS_FULL;
  ExpectFullGets();
  EXPECT_CALL(*mock_cpuacct_controller_, GetSchedulerHistograms())
      .WillRepeatedly(Return(Status(NOT_FOUND, "")));
  expected_stats_.clear_histograms();

  ContainerStats stats;
  EXPECT_OK(handler_->Stats(type, &stats));
  EXPECT_THAT(stats.cpu(), EqualsInitializedProto(expected_stats_));
}

TEST_F(CpuStatsTest, StatsThrottlingNotFound) {
  Container::StatsType type = Container::STATS_FULL;
  ExpectFullGets();
  EXPECT_CALL(*mock_cpu_controller_, GetThrottlingStats())
      .WillRepeatedly(Return(Status(NOT_FOUND, "")));
  expected_stats_.clear_throttling_data();

  ContainerStats stats;
  EXPECT_OK(handler_->Stats(type, &stats));
  EXPECT_THAT(stats.cpu(), EqualsInitializedProto(expected_stats_));
}

TEST_F(CpuResourceHandlerTest, UpdateDiffEmpty) {
  ContainerSpec spec;

  EXPECT_OK(handler_->Update(spec, Container::UPDATE_DIFF));
}


TEST_F(CpuResourceHandlerTest, UpdateDiffSwitchingLatencyFails) {
  ContainerSpec spec;
  spec.mutable_cpu()->set_scheduling_latency(PREMIER);
  EXPECT_CALL(*mock_cpu_controller_, GetLatency())
      .WillOnce(Return(PRIORITY));

  // Trying to update latency to PREMIER should fail.
  EXPECT_NOT_OK(handler_->Update(spec, Container::UPDATE_DIFF));
}

TEST_F(CpuResourceHandlerTest, UpdateDiffNoLatencySpecPasses) {
  ContainerSpec spec;
  // Add empty cpu spec.
  spec.mutable_cpu();
  EXPECT_CALL(*mock_cpu_controller_, GetLatency())
      .WillOnce(Return(PRIORITY));

  EXPECT_OK(handler_->Update(spec, Container::UPDATE_DIFF));
}

TEST_F(CpuResourceHandlerTest, UpdateThroughputSucceeds) {
  ContainerSpec spec;
  spec.mutable_cpu()->set_limit(42);
  for (auto policy : kUpdatePolicy) {
    EXPECT_CALL(*mock_cpu_controller_, GetLatency())
        .WillOnce(Return(PRIORITY));
    EXPECT_CALL(*mock_cpu_controller_, SetMilliCpus(42))
        .WillOnce(Return(Status::OK));

    EXPECT_OK(handler_->Update(spec, policy));
  }
}

TEST_F(CpuResourceHandlerTest, UpdateThroughputFails) {
  ContainerSpec spec;
  spec.mutable_cpu()->set_limit(42);

  for (auto policy : kUpdatePolicy) {
    EXPECT_CALL(*mock_cpu_controller_, GetLatency())
        .WillOnce(Return(PRIORITY));
    EXPECT_CALL(*mock_cpu_controller_, SetMilliCpus(42))
        .WillRepeatedly(Return(Status::CANCELLED));

    EXPECT_NOT_OK(handler_->Update(spec, policy));
  }
}

TEST_F(CpuResourceHandlerTest, UpdateMaxThroughputSucceeds) {
  ContainerSpec spec;
  spec.mutable_cpu()->set_max_limit(42);

  for (auto policy : kUpdatePolicy) {
    EXPECT_CALL(*mock_cpu_controller_, GetLatency())
        .WillOnce(Return(PRIORITY));
    EXPECT_CALL(*mock_cpu_controller_, SetMaxMilliCpus(42))
        .WillOnce(Return(Status::OK));

    EXPECT_OK(handler_->Update(spec, policy));
  }
}

TEST_F(CpuResourceHandlerTest, UpdateMaxThroughputFails) {
  ContainerSpec spec;
  spec.mutable_cpu()->set_max_limit(42);

  for (auto policy : kUpdatePolicy) {
    EXPECT_CALL(*mock_cpu_controller_, GetLatency())
        .WillOnce(Return(PRIORITY));
    EXPECT_CALL(*mock_cpu_controller_, SetMaxMilliCpus(42))
        .WillRepeatedly(Return(Status::CANCELLED));

    EXPECT_NOT_OK(handler_->Update(spec, policy));
  }
}

TEST_F(CpuResourceHandlerTest, UpdateMaskSucceeds) {
  ContainerSpec spec;
  CpuMask(42)
      .WriteToProtobuf(spec.mutable_cpu()->mutable_mask()->mutable_data());

  for (auto policy : kUpdatePolicy) {
    EXPECT_CALL(*mock_cpu_controller_, GetLatency())
        .WillOnce(Return(PRIORITY));
    // TODO(jnagal): Check actual value passed down with CpuMask
    // comparator.
    EXPECT_CALL(*mock_cpuset_controller_, SetCpuMask(testing::_))
         .WillOnce(Return(Status::OK));

    EXPECT_OK(handler_->Update(spec, policy));
  }
}

TEST_F(CpuResourceHandlerTest, UpdateMaskNoCpuset) {
  SetUpHandler(false);

  ContainerSpec spec;
  CpuMask(42)
      .WriteToProtobuf(spec.mutable_cpu()->mutable_mask()->mutable_data());

  for (auto policy : kUpdatePolicy) {
    EXPECT_CALL(*mock_cpu_controller_, GetLatency())
        .WillOnce(Return(PRIORITY));

    EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT,
                      handler_->Update(spec, policy));
  }
}

TEST_F(CpuResourceHandlerTest, UpdateMaskFails) {
  ContainerSpec spec;
  CpuMask(42)
      .WriteToProtobuf(spec.mutable_cpu()->mutable_mask()->mutable_data());

  for (auto policy : kUpdatePolicy) {
    EXPECT_CALL(*mock_cpu_controller_, GetLatency())
        .WillOnce(Return(PRIORITY));
    EXPECT_CALL(*mock_cpuset_controller_, SetCpuMask(testing::_))
        .WillRepeatedly(Return(Status::CANCELLED));

    EXPECT_NOT_OK(handler_->Update(spec, policy));
  }
}

TEST_F(CpuResourceHandlerTest, UpdateReplaceEmpty) {
  ContainerSpec spec;

  EXPECT_OK(handler_->Update(spec, Container::UPDATE_REPLACE));
}

TEST_F(CpuResourceHandlerTest, UpdateReplaceEmptyWithDefaultLatency) {
  ContainerSpec spec;
  spec.mutable_cpu();

  EXPECT_CALL(*mock_cpu_controller_, GetLatency())
      .WillOnce(Return(PRIORITY));

  // Staying with default Latency is ok.
  EXPECT_OK(handler_->Update(spec, Container::UPDATE_REPLACE));
}

TEST_F(CpuResourceHandlerTest, UpdateReplaceEmptyWithNonDefaultLatency) {
  ContainerSpec spec;
  spec.mutable_cpu();

  // Latency was previously set to PREMIER. Empty spec means override to
  // default.
  EXPECT_CALL(*mock_cpu_controller_, GetLatency())
      .WillOnce(Return(PREMIER));

  // changing latency is not allowed.
  EXPECT_NOT_OK(handler_->Update(spec, Container::UPDATE_REPLACE));
}

TEST_F(CpuResourceHandlerTest, UpdateReplaceSwitchingLatencyFails) {
  ContainerSpec spec;
  spec.mutable_cpu()->set_scheduling_latency(PREMIER);
  EXPECT_CALL(*mock_cpu_controller_, GetLatency()).WillOnce(Return(PRIORITY));

  // Trying to update latency to PREMIER should fail.
  EXPECT_NOT_OK(handler_->Update(spec, Container::UPDATE_REPLACE));
}

TEST_F(CpuResourceHandlerTest, UpdateLatencyNotFoundAndNotSet) {
  ContainerSpec spec;
  spec.mutable_cpu()->set_max_limit(42);

  for (auto policy : kUpdatePolicy) {
    EXPECT_CALL(*mock_cpu_controller_, GetLatency())
        .WillOnce(Return(Status(NOT_FOUND, "")));
    EXPECT_CALL(*mock_cpu_controller_, SetMaxMilliCpus(42))
        .WillRepeatedly(Return(Status::OK));

    // Latency update on a machine without latency support is ignored.
    EXPECT_OK(handler_->Update(spec, policy));
  }
}

// Notifications not implemented.
TEST_F(CpuResourceHandlerTest, NotificationsUnimplemented) {
  EventSpec spec;
  StatusOr<Container::NotificationId> statusor =
      handler_->RegisterNotification(spec, nullptr);
  EXPECT_NOT_OK(statusor);
  EXPECT_EQ(NOT_FOUND, statusor.status().error_code());
}

class CpuResourceHandlerSpecTest : public CpuResourceHandlerTest {
 public:
  virtual void SetUp() {
    CpuResourceHandlerTest::SetUp();
    EXPECT_CALL(*mock_cpu_controller_, GetMilliCpus())
        .WillRepeatedly(Return(StatusOr<int64>(123)));
    EXPECT_CALL(*mock_cpu_controller_, GetMaxMilliCpus())
        .WillRepeatedly(Return(StatusOr<int64>(456)));
    EXPECT_CALL(*mock_cpuset_controller_, GetCpuMask())
        .WillRepeatedly(Return(StatusOr<CpuMask>(CpuMask(789))));
  }
};

TEST_F(CpuResourceHandlerSpecTest, AllSucceed) {
  ContainerSpec spec;
  EXPECT_OK(handler_->Spec(&spec));
  EXPECT_EQ(123, spec.cpu().limit());
  EXPECT_EQ(456, spec.cpu().max_limit());
  EXPECT_EQ(789, CpuMask(spec.cpu().mask().data()));
}

TEST_F(CpuResourceHandlerSpecTest, NoCpuSetControllerSuccess) {
  CpuResourceHandlerTest::SetUpHandler(false);
  EXPECT_CALL(*mock_cpu_controller_, GetMilliCpus())
      .WillRepeatedly(Return(StatusOr<int64>(123)));
  EXPECT_CALL(*mock_cpu_controller_, GetMaxMilliCpus())
      .WillRepeatedly(Return(StatusOr<int64>(456)));

  ContainerSpec spec;
  EXPECT_OK(handler_->Spec(&spec));
  EXPECT_EQ(123, spec.cpu().limit());
  EXPECT_EQ(456, spec.cpu().max_limit());
  EXPECT_EQ(0, spec.cpu().mask().data_size());
}

TEST_F(CpuResourceHandlerSpecTest, FailLimit) {
  EXPECT_CALL(*mock_cpu_controller_, GetMilliCpus())
      .WillOnce(Return(::util::Status(::util::error::INVALID_ARGUMENT, "")));
  ContainerSpec spec;
  EXPECT_NOT_OK(handler_->Spec(&spec));
}

TEST_F(CpuResourceHandlerSpecTest, FailMaxLimit) {
  EXPECT_CALL(*mock_cpu_controller_, GetMaxMilliCpus())
      .WillOnce(Return(::util::Status(::util::error::INVALID_ARGUMENT, "")));
  ContainerSpec spec;
  EXPECT_NOT_OK(handler_->Spec(&spec));
}

TEST_F(CpuResourceHandlerSpecTest, FailGetCpuMask) {
  EXPECT_CALL(*mock_cpuset_controller_, GetCpuMask())
      .WillOnce(Return(::util::Status(::util::error::INVALID_ARGUMENT, "")));
  ContainerSpec spec;
  EXPECT_NOT_OK(handler_->Spec(&spec));
}

}  // namespace lmctfy
}  // namespace containers
