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
#include "util/errors_test_util.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "util/os/core/cpu_set.h"
#include "util/task/codes.pb.h"

using ::system_api::KernelAPIMock;
using ::std::unique_ptr;
using ::std::vector;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::StrictMock;
using ::testing::_;
using ::util::Status;
using ::util::StatusOr;
using ::util_os_core::UInt64ToCpuSet;

namespace containers {
namespace lmctfy {

static const char kContainerName[] = "/test";
static const char kBatchHierarchyPath[] = "/batch/test";

class CpuResourceHandlerFactoryTest : public ::testing::Test {
 public:
  virtual void SetUp() {
    mock_kernel_.reset(new StrictMock<KernelAPIMock>());
    mock_cpu_controller_.reset(new StrictMockCpuController());
    mock_cpuacct_controller_.reset(new StrictMockCpuAcctController());
    mock_cpuset_controller_.reset(new StrictMockCpusetController());
    mock_cgroup_factory_.reset(new NiceMockCgroupFactory());
    mock_cpu_controller_factory_ =
        new StrictMockCpuControllerFactory(mock_cgroup_factory_.get());
    mock_cpuacct_controller_factory_ =
        new StrictMockCpuAcctControllerFactory(mock_cgroup_factory_.get());
    mock_cpuset_controller_factory_ =
        new StrictMockCpusetControllerFactory(mock_cgroup_factory_.get());
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
  unique_ptr<MockCpuController> mock_cpu_controller_;
  unique_ptr<MockCpuAcctController> mock_cpuacct_controller_;
  unique_ptr<MockCpusetController> mock_cpuset_controller_;
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
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists(kContainerName))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cpu_controller_factory_, Get(kContainerName))
      .WillRepeatedly(Return(mock_cpu_controller_.get()));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Get(kContainerName))
      .WillRepeatedly(Return(mock_cpuacct_controller_.get()));
  EXPECT_CALL(*mock_cpuset_controller_factory_, Get(kContainerName))
      .WillRepeatedly(Return(mock_cpuset_controller_.get()));

  StatusOr<ResourceHandler *> statusor = CallGetResourceHandler(kContainerName);
  ASSERT_TRUE(statusor.ok());
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  unique_ptr<ResourceHandler> handler(statusor.ValueOrDie());
  EXPECT_EQ(RESOURCE_CPU, handler->type());
  EXPECT_EQ(kContainerName, handler->container_name());
  // Controllers ownership transferred to resource handler.
  mock_cpu_controller_.release();
  mock_cpuset_controller_.release();
  mock_cpuacct_controller_.release();
}

TEST_F(CpuResourceHandlerFactoryTest, GetBatchContainer) {
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists(kContainerName))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists(kBatchHierarchyPath))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cpu_controller_factory_, Get(kBatchHierarchyPath))
      .WillRepeatedly(Return(mock_cpu_controller_.get()));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Get(kBatchHierarchyPath))
      .WillRepeatedly(Return(mock_cpuacct_controller_.get()));
  EXPECT_CALL(*mock_cpuset_controller_factory_, Get(kContainerName))
      .WillRepeatedly(Return(mock_cpuset_controller_.get()));

  StatusOr<ResourceHandler *> statusor = CallGetResourceHandler(kContainerName);
  ASSERT_TRUE(statusor.ok());
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  unique_ptr<ResourceHandler> handler(statusor.ValueOrDie());
  EXPECT_EQ(RESOURCE_CPU, handler->type());
  EXPECT_EQ(kContainerName, handler->container_name());
  // Controllers ownership transferred to resource handler.
  mock_cpu_controller_.release();
  mock_cpuset_controller_.release();
  mock_cpuacct_controller_.release();
}

TEST_F(CpuResourceHandlerFactoryTest, GetUnknownContainer) {
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists(kContainerName))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists(kBatchHierarchyPath))
      .WillRepeatedly(Return(false));

  EXPECT_ERROR_CODE(::util::error::NOT_FOUND,
                    CallGetResourceHandler(kContainerName));
}

TEST_F(CpuResourceHandlerFactoryTest, GetCpuControllerFails) {
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists(kContainerName))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cpu_controller_factory_, Get(kContainerName))
      .WillRepeatedly(Return(Status::CANCELLED));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Get(kContainerName))
      .WillRepeatedly(Return(mock_cpuacct_controller_.get()));
  EXPECT_CALL(*mock_cpuset_controller_factory_, Get(kContainerName))
      .WillRepeatedly(Return(mock_cpuset_controller_.get()));

  EXPECT_EQ(Status::CANCELLED, CallGetResourceHandler(kContainerName).status());
}

TEST_F(CpuResourceHandlerFactoryTest, GetCpuAcctControllerFails) {
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists(kContainerName))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cpu_controller_factory_, Get(kContainerName))
      .WillRepeatedly(Return(mock_cpu_controller_.get()));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Get(kContainerName))
      .WillRepeatedly(Return(Status::CANCELLED));
  EXPECT_CALL(*mock_cpuset_controller_factory_, Get(kContainerName))
      .WillRepeatedly(Return(mock_cpuset_controller_.get()));

  EXPECT_EQ(Status::CANCELLED, CallGetResourceHandler(kContainerName).status());
}

TEST_F(CpuResourceHandlerFactoryTest, GetCpusetControllerFails) {
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists(kContainerName))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cpu_controller_factory_, Get(kContainerName))
      .WillRepeatedly(Return(mock_cpu_controller_.get()));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Get(kContainerName))
      .WillRepeatedly(Return(mock_cpuacct_controller_.get()));
  EXPECT_CALL(*mock_cpuset_controller_factory_, Get(kContainerName))
      .WillRepeatedly(Return(Status::CANCELLED));

  // cpuset controller is allowed to fail. We'll just end up using a stub.
  StatusOr<ResourceHandler *> statusor = CallGetResourceHandler(kContainerName);
  ASSERT_TRUE(statusor.ok());
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  unique_ptr<ResourceHandler> handler(statusor.ValueOrDie());
  EXPECT_EQ(RESOURCE_CPU, handler->type());
  EXPECT_EQ(kContainerName, handler->container_name());
  // Controllers ownership transferred to resource handler (except cpuset).
  mock_cpu_controller_.release();
  mock_cpuacct_controller_.release();
}

// Tests for Create().

TEST_F(CpuResourceHandlerFactoryTest,
       CreateResourceHandlerSucceedsTopLevelTask) {
  ContainerSpec spec;
  spec.mutable_cpu()->set_scheduling_latency(PRIORITY);

  EXPECT_CALL(*mock_cpu_controller_factory_, Exists("/"))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cpu_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_cpu_controller_.get()));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_cpuacct_controller_.get()));
  EXPECT_CALL(*mock_cpuset_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_cpuset_controller_.get()));

  StatusOr<ResourceHandler *> statusor =
      CallCreateResourceHandler(kContainerName, spec);
  ASSERT_TRUE(statusor.ok());
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  unique_ptr<ResourceHandler> handler(statusor.ValueOrDie());
  EXPECT_EQ(RESOURCE_CPU, handler->type());
  EXPECT_EQ(kContainerName, handler->container_name());
  // Controllers ownership transferred to resource handler.
  mock_cpu_controller_.release();
  mock_cpuset_controller_.release();
  mock_cpuacct_controller_.release();
}

TEST_F(CpuResourceHandlerFactoryTest,
       CreateResourceHandlerSucceedsTopLevelDefaultTask) {
  ContainerSpec spec;

  // The default latency of NORMAL corresponds to a batch task.
  const string kBatchContainerName = kBatchHierarchyPath;
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists("/"))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cpu_controller_factory_, Create(kBatchContainerName))
      .WillRepeatedly(Return(mock_cpu_controller_.get()));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Create(kBatchContainerName))
      .WillRepeatedly(Return(mock_cpuacct_controller_.get()));
  EXPECT_CALL(*mock_cpuset_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_cpuset_controller_.get()));

  StatusOr<ResourceHandler *> statusor =
      CallCreateResourceHandler(kContainerName, spec);
  ASSERT_TRUE(statusor.ok());
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  unique_ptr<ResourceHandler> handler(statusor.ValueOrDie());
  EXPECT_EQ(RESOURCE_CPU, handler->type());
  EXPECT_EQ(kContainerName, handler->container_name());

  // Controllers ownership transferred to resource handler.
  mock_cpu_controller_.release();
  mock_cpuset_controller_.release();
  mock_cpuacct_controller_.release();
}

TEST_F(CpuResourceHandlerFactoryTest,
       CreateResourceHandlerSucceedsTopLevelBatchTask) {
  ContainerSpec spec;
  spec.mutable_cpu()->set_scheduling_latency(BEST_EFFORT);

  const string kBatchContainerName = kBatchHierarchyPath;
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists("/"))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cpu_controller_factory_, Create(kBatchContainerName))
      .WillRepeatedly(Return(mock_cpu_controller_.get()));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Create(kBatchContainerName))
      .WillRepeatedly(Return(mock_cpuacct_controller_.get()));
  EXPECT_CALL(*mock_cpuset_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_cpuset_controller_.get()));

  StatusOr<ResourceHandler *> statusor =
      CallCreateResourceHandler(kContainerName, spec);
  ASSERT_TRUE(statusor.ok());
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  unique_ptr<ResourceHandler> handler(statusor.ValueOrDie());
  EXPECT_EQ(RESOURCE_CPU, handler->type());
  EXPECT_EQ(kContainerName, handler->container_name());

  // Controllers ownership transferred to resource handler.
  mock_cpu_controller_.release();
  mock_cpuset_controller_.release();
  mock_cpuacct_controller_.release();
}

TEST_F(CpuResourceHandlerFactoryTest,
       CreateResourceHandlerSucceedsLsTaskUnderAlloc) {
  ContainerSpec spec;
  spec.mutable_cpu()->set_scheduling_latency(PRIORITY);

  const char kContainerFullName[] = "/alloc/test";
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists("/alloc"))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cpu_controller_factory_, Create(kContainerFullName))
      .WillRepeatedly(Return(mock_cpu_controller_.get()));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Create(kContainerFullName))
      .WillRepeatedly(Return(mock_cpuacct_controller_.get()));
  // cpuset is flat.
  EXPECT_CALL(*mock_cpuset_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_cpuset_controller_.get()));

  StatusOr<ResourceHandler *> statusor =
      CallCreateResourceHandler(kContainerFullName, spec);
  ASSERT_TRUE(statusor.ok());
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  unique_ptr<ResourceHandler> handler(statusor.ValueOrDie());
  EXPECT_EQ(RESOURCE_CPU, handler->type());
  EXPECT_EQ(kContainerFullName, handler->container_name());
  // Controllers ownership transferred to resource handler.
  mock_cpu_controller_.release();
  mock_cpuset_controller_.release();
  mock_cpuacct_controller_.release();
}

TEST_F(CpuResourceHandlerFactoryTest,
       CreateResourceHandlerSucceedsTaskUnderBatchAlloc) {
  ContainerSpec spec;
  spec.mutable_cpu()->set_scheduling_latency(BEST_EFFORT);

  const char kContainerFullName[] = "/alloc/test";
  const char kExpectedName[] = "/batch/alloc/test";
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists("/alloc"))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists("/batch/alloc"))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cpu_controller_factory_, Create(kExpectedName))
      .WillRepeatedly(Return(mock_cpu_controller_.get()));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Create(kExpectedName))
      .WillRepeatedly(Return(mock_cpuacct_controller_.get()));
  // cpuset is flat.
  EXPECT_CALL(*mock_cpuset_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_cpuset_controller_.get()));

  StatusOr<ResourceHandler *> statusor =
      CallCreateResourceHandler(kContainerFullName, spec);
  ASSERT_TRUE(statusor.ok());
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  unique_ptr<ResourceHandler> handler(statusor.ValueOrDie());
  EXPECT_EQ(RESOURCE_CPU, handler->type());
  EXPECT_EQ(kContainerFullName, handler->container_name());
  // Controllers ownership transferred to resource handler.
  mock_cpu_controller_.release();
  mock_cpuset_controller_.release();
  mock_cpuacct_controller_.release();
}


TEST_F(CpuResourceHandlerFactoryTest, CreateCpuControllerFails) {
  ContainerSpec spec;
  spec.mutable_cpu()->set_scheduling_latency(PRIORITY);

  EXPECT_CALL(*mock_cpu_controller_factory_, Exists("/"))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cpu_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(Status::CANCELLED));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_cpuacct_controller_.get()));
  EXPECT_CALL(*mock_cpuset_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_cpuset_controller_.get()));

  EXPECT_EQ(Status::CANCELLED,
            CallCreateResourceHandler(kContainerName, spec).status());
}

TEST_F(CpuResourceHandlerFactoryTest, CreateCpuAcctControllerFails) {
  ContainerSpec spec;
  spec.mutable_cpu()->set_scheduling_latency(PRIORITY);

  EXPECT_CALL(*mock_cpu_controller_factory_, Exists("/"))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cpu_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_cpu_controller_.get()));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(Status::CANCELLED));
  EXPECT_CALL(*mock_cpuset_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_cpuset_controller_.get()));

  EXPECT_EQ(Status::CANCELLED,
            CallCreateResourceHandler(kContainerName, spec).status());
}

TEST_F(CpuResourceHandlerFactoryTest, CreateCpusetControllerFails) {
  ContainerSpec spec;
  spec.mutable_cpu()->set_scheduling_latency(PRIORITY);

  EXPECT_CALL(*mock_cpu_controller_factory_, Exists("/"))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cpu_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_cpu_controller_.get()));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_cpuacct_controller_.get()));
  EXPECT_CALL(*mock_cpuset_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(Status::CANCELLED));

  // cpuset failure is ignored. A stub controller is used instead.
  StatusOr<ResourceHandler *> statusor =
      CallCreateResourceHandler(kContainerName, spec);
  ASSERT_TRUE(statusor.ok());
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  unique_ptr<ResourceHandler> handler(statusor.ValueOrDie());
  EXPECT_EQ(RESOURCE_CPU, handler->type());
  EXPECT_EQ(kContainerName, handler->container_name());
  // Controllers ownership transferred to resource handler (except cpuset).
  mock_cpu_controller_.release();
  mock_cpuacct_controller_.release();
}

TEST_F(CpuResourceHandlerFactoryTest, CreateResourceHandlerFailsMissingParent) {
  ContainerSpec spec;

  const char kContainerFullName[] = "/alloc/test";
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists("/alloc"))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists("/batch/alloc"))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*mock_cpu_controller_factory_, Create(kContainerFullName))
      .WillRepeatedly(Return(mock_cpu_controller_.get()));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Create(kContainerFullName))
      .WillRepeatedly(Return(mock_cpuacct_controller_.get()));
  EXPECT_CALL(*mock_cpuset_controller_factory_, Create(kContainerFullName))
      .WillRepeatedly(Return(mock_cpuset_controller_.get()));

  StatusOr<ResourceHandler *> statusor =
            CallCreateResourceHandler(kContainerFullName, spec);
  EXPECT_NOT_OK(statusor);
  EXPECT_EQ(::util::error::NOT_FOUND, statusor.status().error_code());
}

TEST_F(CpuResourceHandlerFactoryTest, CreateSucceeds) {
  ContainerSpec spec;

  // The default latency of NORMAL corresponds to a batch task.
  const string kBatchContainerName = kBatchHierarchyPath;
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists("/"))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cpu_controller_factory_, Create(kBatchContainerName))
      .WillRepeatedly(Return(mock_cpu_controller_.get()));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Create(kBatchContainerName))
      .WillRepeatedly(Return(mock_cpuacct_controller_.get()));
  EXPECT_CALL(*mock_cpuset_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_cpuset_controller_.get()));

  // Latency should be set to NORMAL (kernel default).
  EXPECT_CALL(*mock_cpu_controller_.get(), SetLatency(NORMAL))
      .WillRepeatedly(Return(Status::OK));
  EXPECT_CALL(*mock_cpuacct_controller_.get(), SetupHistograms())
      .WillRepeatedly(Return(Status::OK));

  StatusOr<ResourceHandler *> statusor = CallCreate(kContainerName, spec);
  ASSERT_TRUE(statusor.ok());
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  unique_ptr<ResourceHandler> handler(statusor.ValueOrDie());
  EXPECT_EQ(RESOURCE_CPU, handler->type());
  EXPECT_EQ(kContainerName, handler->container_name());

  // Controllers ownership transferred to resource handler.
  mock_cpu_controller_.release();
  mock_cpuset_controller_.release();
  mock_cpuacct_controller_.release();
}

TEST_F(CpuResourceHandlerFactoryTest, CreateCpuCreateFailes) {
  ContainerSpec spec;

  // The default latency of NORMAL corresponds to a batch task.
  const string kBatchContainerName = kBatchHierarchyPath;
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists("/"))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cpu_controller_factory_, Create(kBatchContainerName))
      .WillRepeatedly(Return(Status::CANCELLED));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Create(kBatchContainerName))
      .WillRepeatedly(Return(mock_cpuacct_controller_.get()));
  EXPECT_CALL(*mock_cpuset_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_cpuset_controller_.get()));

  EXPECT_CALL(*mock_cpuacct_controller_.get(), SetupHistograms())
      .WillRepeatedly(Return(Status::OK));

  EXPECT_EQ(Status::CANCELLED, CallCreate(kContainerName, spec).status());
}

TEST_F(CpuResourceHandlerFactoryTest, CreateSetLatencyNotFound) {
  ContainerSpec spec;
  spec.mutable_cpu()->set_scheduling_latency(PRIORITY);

  EXPECT_CALL(*mock_cpu_controller_factory_, Exists("/"))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cpu_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_cpu_controller_.get()));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_cpuacct_controller_.get()));
  EXPECT_CALL(*mock_cpuset_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_cpuset_controller_.get()));

  EXPECT_CALL(*mock_cpu_controller_.get(), SetLatency(PRIORITY))
      .WillRepeatedly(Return(Status(::util::error::NOT_FOUND, "")));
  EXPECT_CALL(*mock_cpuacct_controller_.get(), SetupHistograms())
      .WillRepeatedly(Return(Status::OK));

  EXPECT_CALL(*mock_cpu_controller_, GetLatency())
      .WillRepeatedly(Return(PRIORITY));
  EXPECT_CALL(*mock_cpuset_controller_, InheritCpuMask())
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_cpuset_controller_, InheritMemoryNodes())
      .WillOnce(Return(Status::OK));

  // We ignore SetLatency() when it is NOT_FOUND.
  StatusOr<ResourceHandler *> statusor = CallCreate(kContainerName, spec);
  ASSERT_TRUE(statusor.ok());
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  unique_ptr<ResourceHandler> handler(statusor.ValueOrDie());
  EXPECT_EQ(RESOURCE_CPU, handler->type());
  EXPECT_EQ(kContainerName, handler->container_name());

  // Controllers ownership transferred to resource handler.
  mock_cpu_controller_.release();
  mock_cpuset_controller_.release();
  mock_cpuacct_controller_.release();
}

TEST_F(CpuResourceHandlerFactoryTest, CreateSetupHistogramsNotFound) {
  ContainerSpec spec;

  // The default latency of NORMAL corresponds to a batch task.
  const string kBatchContainerName = kBatchHierarchyPath;
  EXPECT_CALL(*mock_cpu_controller_factory_, Exists("/"))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cpu_controller_factory_, Create(kBatchContainerName))
      .WillRepeatedly(Return(mock_cpu_controller_.get()));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Create(kBatchContainerName))
      .WillRepeatedly(Return(mock_cpuacct_controller_.get()));
  EXPECT_CALL(*mock_cpuset_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_cpuset_controller_.get()));

  // Latency should be set to NORMAL (kernel default).
  EXPECT_CALL(*mock_cpu_controller_.get(), SetLatency(NORMAL))
      .WillRepeatedly(Return(Status::OK));
  EXPECT_CALL(*mock_cpuacct_controller_.get(), SetupHistograms())
      .WillRepeatedly(Return(Status(::util::error::NOT_FOUND, "")));

  // We ignore SetupHistograms() not being supported.
  StatusOr<ResourceHandler *> statusor = CallCreate(kContainerName, spec);
  ASSERT_TRUE(statusor.ok());
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  unique_ptr<ResourceHandler> handler(statusor.ValueOrDie());
  EXPECT_EQ(RESOURCE_CPU, handler->type());
  EXPECT_EQ(kContainerName, handler->container_name());

  // Controllers ownership transferred to resource handler.
  mock_cpu_controller_.release();
  mock_cpuset_controller_.release();
  mock_cpuacct_controller_.release();
}

TEST_F(CpuResourceHandlerFactoryTest, CreateSetLatencyFails) {
  ContainerSpec spec;
  CpuSpec *cpu_spec = spec.mutable_cpu();
  cpu_spec->set_scheduling_latency(PRIORITY);

  EXPECT_CALL(*mock_cpu_controller_factory_, Exists("/"))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cpu_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_cpu_controller_.get()));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_cpuacct_controller_.get()));
  EXPECT_CALL(*mock_cpuset_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_cpuset_controller_.get()));

  // Fail to set up latency.
  EXPECT_CALL(*mock_cpu_controller_.get(), SetLatency(PRIORITY))
      .WillRepeatedly(Return(Status::CANCELLED));
  EXPECT_CALL(*mock_cpuacct_controller_.get(), SetupHistograms())
      .WillRepeatedly(Return(Status::OK));

  EXPECT_EQ(Status::CANCELLED, CallCreate(kContainerName, spec).status());

  // Controllers get cleaned up at resource handler creation failure.
  mock_cpu_controller_.release();
  mock_cpuset_controller_.release();
  mock_cpuacct_controller_.release();
}


TEST_F(CpuResourceHandlerFactoryTest, CreateSettingHistogramsFails) {
  ContainerSpec spec;
  spec.mutable_cpu()->set_scheduling_latency(PRIORITY);

  EXPECT_CALL(*mock_cpu_controller_factory_, Exists("/"))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cpu_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_cpu_controller_.get()));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_cpuacct_controller_.get()));
  EXPECT_CALL(*mock_cpuset_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_cpuset_controller_.get()));

  EXPECT_CALL(*mock_cpu_controller_.get(), SetLatency(PRIORITY))
      .WillRepeatedly(Return(Status::OK));

  // Fail to set up histograms.
  EXPECT_CALL(*mock_cpuacct_controller_.get(), SetupHistograms())
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED, CallCreate(kContainerName, spec).status());

  // Controllers get cleaned up at resource handler creation failure.
  mock_cpu_controller_.release();
  mock_cpuset_controller_.release();
  mock_cpuacct_controller_.release();
}

// Tests for InitMachine().

TEST_F(CpuResourceHandlerFactoryTest, InitMachineSuccess) {
  InitSpec spec;

  EXPECT_CALL(*mock_cpu_controller_factory_, Create("/batch"))
      .WillOnce(Return(mock_cpu_controller_.get()));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Create("/batch"))
      .WillOnce(Return(mock_cpuacct_controller_.get()));

  EXPECT_CALL(*mock_cpu_controller_, SetMilliCpus(0))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_cpuacct_controller_, SetupHistograms())
      .WillOnce(Return(Status::OK));

  EXPECT_OK(CallInitMachine(spec));

  // Controllers are owned by the caller of Create().
  mock_cpu_controller_.release();
  mock_cpuacct_controller_.release();
}

TEST_F(CpuResourceHandlerFactoryTest, InitMachineCpuCreateFails) {
  InitSpec spec;

  EXPECT_CALL(*mock_cpu_controller_factory_, Create("/batch"))
      .WillOnce(Return(Status::CANCELLED));

  EXPECT_NOT_OK(CallInitMachine(spec));
}

TEST_F(CpuResourceHandlerFactoryTest, InitMachineCpuAcctCreateFails) {
  InitSpec spec;

  EXPECT_CALL(*mock_cpu_controller_factory_, Create("/batch"))
      .WillOnce(Return(mock_cpu_controller_.get()));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Create("/batch"))
      .WillOnce(Return(Status::CANCELLED));

  EXPECT_CALL(*mock_cpu_controller_, SetMilliCpus(0))
      .WillRepeatedly(Return(Status::OK));

  EXPECT_NOT_OK(CallInitMachine(spec));

  // Controllers are owned by the caller of Create().
  mock_cpu_controller_.release();
}

TEST_F(CpuResourceHandlerFactoryTest, InitMachineAlreadyInitializedSuccess) {
  InitSpec spec;

  EXPECT_CALL(*mock_cpu_controller_factory_, Create("/batch"))
      .WillRepeatedly(Return(Status(::util::error::ALREADY_EXISTS, "")));
  EXPECT_CALL(*mock_cpu_controller_factory_, Get("/batch"))
      .WillOnce(Return(mock_cpu_controller_.get()));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Create("/batch"))
      .WillRepeatedly(Return(Status(::util::error::ALREADY_EXISTS, "")));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Get("/batch"))
      .WillOnce(Return(mock_cpuacct_controller_.get()));

  EXPECT_CALL(*mock_cpu_controller_, SetMilliCpus(0))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_cpuacct_controller_, SetupHistograms())
      .WillOnce(Return(Status::OK));

  EXPECT_OK(CallInitMachine(spec));

  // Controllers are owned by the caller of Create().
  mock_cpu_controller_.release();
  mock_cpuacct_controller_.release();
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

  EXPECT_CALL(*mock_cpu_controller_factory_, Create("/batch"))
      .WillRepeatedly(Return(Status(::util::error::ALREADY_EXISTS, "")));
  EXPECT_CALL(*mock_cpu_controller_factory_, Get("/batch"))
      .WillOnce(Return(mock_cpu_controller_.get()));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Create("/batch"))
      .WillRepeatedly(Return(Status(::util::error::ALREADY_EXISTS, "")));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Get("/batch"))
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_CALL(*mock_cpu_controller_, SetMilliCpus(0))
      .WillRepeatedly(Return(Status::OK));

  EXPECT_NOT_OK(CallInitMachine(spec));

  // Controllers are owned by the caller of Create().
  mock_cpu_controller_.release();
}

TEST_F(CpuResourceHandlerFactoryTest, InitMachineSetMilliCpusFails) {
  InitSpec spec;

  EXPECT_CALL(*mock_cpu_controller_factory_, Create("/batch"))
      .WillOnce(Return(mock_cpu_controller_.get()));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Create("/batch"))
      .WillOnce(Return(mock_cpuacct_controller_.get()));

  EXPECT_CALL(*mock_cpu_controller_, SetMilliCpus(0))
      .WillOnce(Return(Status::CANCELLED));
  EXPECT_CALL(*mock_cpuacct_controller_, SetupHistograms())
      .WillRepeatedly(Return(Status::OK));

  EXPECT_NOT_OK(CallInitMachine(spec));

  // Controllers are owned by the caller of Create().
  mock_cpu_controller_.release();
  mock_cpuacct_controller_.release();
}

TEST_F(CpuResourceHandlerFactoryTest, InitMachineSetupHistogramsFails) {
  InitSpec spec;

  EXPECT_CALL(*mock_cpu_controller_factory_, Create("/batch"))
      .WillOnce(Return(mock_cpu_controller_.get()));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Create("/batch"))
      .WillOnce(Return(mock_cpuacct_controller_.get()));

  EXPECT_CALL(*mock_cpu_controller_, SetMilliCpus(0))
      .WillRepeatedly(Return(Status::OK));
  EXPECT_CALL(*mock_cpuacct_controller_, SetupHistograms())
      .WillOnce(Return(Status::CANCELLED));

  EXPECT_NOT_OK(CallInitMachine(spec));

  // Controllers are owned by the caller of Create().
  mock_cpu_controller_.release();
  mock_cpuacct_controller_.release();
}

TEST_F(CpuResourceHandlerFactoryTest, InitMachineSetupHistogramsNotFound) {
  InitSpec spec;

  EXPECT_CALL(*mock_cpu_controller_factory_, Create("/batch"))
      .WillOnce(Return(mock_cpu_controller_.get()));
  EXPECT_CALL(*mock_cpuacct_controller_factory_, Create("/batch"))
      .WillOnce(Return(mock_cpuacct_controller_.get()));

  EXPECT_CALL(*mock_cpu_controller_, SetMilliCpus(0))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_cpuacct_controller_, SetupHistograms())
      .WillOnce(Return(Status(::util::error::NOT_FOUND, "")));

  // SetupHistogram() is allowed to be NOT_FOUND.
  EXPECT_OK(CallInitMachine(spec));

  // Controllers are owned by the caller of Create().
  mock_cpu_controller_.release();
  mock_cpuacct_controller_.release();
}

class CpuResourceHandlerTest : public ::testing::Test {
 public:
  CpuResourceHandlerTest()
      : kHistoTypes({SERVE, ONCPU, SLEEP, QUEUE_SELF, QUEUE_OTHER, }),
        kUpdatePolicy({Container::UPDATE_DIFF, Container::UPDATE_REPLACE, }) {}

  virtual void SetUp() {
    mock_kernel_.reset(new StrictMock<KernelAPIMock>());
    mock_cpu_controller_ = new StrictMockCpuController();
    mock_cpuacct_controller_ = new StrictMockCpuAcctController();
    mock_cpuset_controller_ = new StrictMockCpusetController();
    handler_.reset(new CpuResourceHandler(kContainerName, mock_kernel_.get(),
                                          mock_cpu_controller_,
                                          mock_cpuacct_controller_,
                                          mock_cpuset_controller_));
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

TEST_F(CpuResourceHandlerTest, StatsSummarySuccess) {
  Container::StatsType type = Container::STATS_SUMMARY;
  ContainerStats stats;

  const uint64 usage = 112233445566;
  const int32 load = 42;
  EXPECT_CALL(*mock_cpuacct_controller_, GetCpuUsageInNs())
      .WillRepeatedly(Return(usage));
  EXPECT_CALL(*mock_cpu_controller_, GetNumRunnable())
      .WillRepeatedly(Return(load));
  EXPECT_OK(handler_->Stats(type, &stats));
  ASSERT_TRUE(stats.has_cpu());
  EXPECT_EQ(usage, stats.cpu().usage());
  EXPECT_EQ(load, stats.cpu().load());
  EXPECT_FALSE(stats.cpu().has_throttling_data());
  EXPECT_EQ(0, stats.cpu().histograms_size());
}

TEST_F(CpuResourceHandlerTest, StatsFullSuccess) {
  Container::StatsType type = Container::STATS_FULL;
  ContainerStats stats;

  const uint64 usage = 112233445566;
  const int32 load = 42;

  // Prepare scheduler histograms.
  ::std::vector<CpuHistogramData *> histograms;
  for (auto type : kHistoTypes) {
    CpuHistogramData *data = new CpuHistogramData();
    data->type = type;
    for (int key = 1; key <= 3; ++key) {
      data->buckets[key * 1000] = 100 * key;
    }
    histograms.push_back(data);
  }

  // Prepare throttling stats.
  struct ThrottlingStats throttling_stats;
  throttling_stats.nr_periods = 100;
  throttling_stats.nr_throttled = 20;
  throttling_stats.throttled_time = 123456789;

  EXPECT_CALL(*mock_cpuacct_controller_, GetCpuUsageInNs())
      .WillRepeatedly(Return(usage));
  EXPECT_CALL(*mock_cpu_controller_, GetNumRunnable())
      .WillRepeatedly(Return(load));
  EXPECT_CALL(*mock_cpuacct_controller_, GetSchedulerHistograms())
      .WillRepeatedly(Return(&histograms));
  EXPECT_CALL(*mock_cpu_controller_, GetThrottlingStats())
      .WillRepeatedly(Return(throttling_stats));
  EXPECT_OK(handler_->Stats(type, &stats));
  ASSERT_TRUE(stats.has_cpu());
  EXPECT_EQ(usage, stats.cpu().usage());
  EXPECT_EQ(load, stats.cpu().load());

  // Verify throttling stats.
  EXPECT_TRUE(stats.cpu().has_throttling_data());
  EXPECT_EQ(stats.cpu().throttling_data().periods(),
            throttling_stats.nr_periods);
  EXPECT_EQ(stats.cpu().throttling_data().throttled_periods(),
            throttling_stats.nr_throttled);
  EXPECT_EQ(stats.cpu().throttling_data().throttled_time(),
            throttling_stats.throttled_time);

  // Verify histogram data.
  EXPECT_EQ(5, stats.cpu().histograms_size());
  for (int i = 0; i < 5; i++) {
    EXPECT_EQ(stats.cpu().histograms(i).type(), kHistoTypes[i]);
    EXPECT_EQ(3, stats.cpu().histograms(i).stat_size());
    for (int index = 0; index < 3; ++index) {
      const HistogramMap_Bucket &stat = stats.cpu().histograms(i).stat(index);
      EXPECT_EQ(1000 * (index + 1), stat.bucket());
      EXPECT_EQ(100 * (index + 1), stat.value());
    }
  }
}

TEST_F(CpuResourceHandlerTest, StatsUsageFails) {
  Container::StatsType type = Container::STATS_FULL;
  ContainerStats stats;

  EXPECT_CALL(*mock_cpuacct_controller_, GetCpuUsageInNs())
      .WillRepeatedly(Return(Status::CANCELLED));
  EXPECT_EQ(Status::CANCELLED, handler_->Stats(type, &stats));
}

TEST_F(CpuResourceHandlerTest, StatsLoadFails) {
  Container::StatsType type = Container::STATS_FULL;
  ContainerStats stats;

  const uint64 usage = 112233445566;

  EXPECT_CALL(*mock_cpuacct_controller_, GetCpuUsageInNs())
      .WillRepeatedly(Return(usage));
  EXPECT_CALL(*mock_cpu_controller_, GetNumRunnable())
      .WillRepeatedly(Return(Status::CANCELLED));
  EXPECT_EQ(Status::CANCELLED, handler_->Stats(type, &stats));
}


TEST_F(CpuResourceHandlerTest, StatsHistogramFails) {
  Container::StatsType type = Container::STATS_FULL;
  ContainerStats stats;

  const uint64 usage = 112233445566;
  const int32 load = 42;

  // Prepare throttling stats.
  struct ThrottlingStats throttling_stats;
  throttling_stats.nr_periods = 100;
  throttling_stats.nr_throttled = 20;
  throttling_stats.throttled_time = 123456789;

  EXPECT_CALL(*mock_cpuacct_controller_, GetCpuUsageInNs())
      .WillRepeatedly(Return(usage));
  EXPECT_CALL(*mock_cpu_controller_, GetNumRunnable())
      .WillRepeatedly(Return(load));
  EXPECT_CALL(*mock_cpuacct_controller_, GetSchedulerHistograms())
      .WillRepeatedly(Return(Status::CANCELLED));
  EXPECT_CALL(*mock_cpu_controller_, GetThrottlingStats())
      .WillRepeatedly(Return(throttling_stats));
  EXPECT_EQ(Status::CANCELLED, handler_->Stats(type, &stats));
}


TEST_F(CpuResourceHandlerTest, StatsThrottlingFails) {
  Container::StatsType type = Container::STATS_FULL;
  ContainerStats stats;

  const uint64 usage = 112233445566;
  const int32 load = 42;

  EXPECT_CALL(*mock_cpuacct_controller_, GetCpuUsageInNs())
      .WillRepeatedly(Return(usage));
  EXPECT_CALL(*mock_cpu_controller_, GetNumRunnable())
      .WillRepeatedly(Return(load));
  EXPECT_CALL(*mock_cpu_controller_, GetThrottlingStats())
      .WillRepeatedly(Return(Status::CANCELLED));
  EXPECT_EQ(Status::CANCELLED, handler_->Stats(type, &stats));
}

TEST_F(CpuResourceHandlerTest, UpdateDiffEmpty) {
  ContainerSpec spec;

  EXPECT_OK(handler_->Update(spec, Container::UPDATE_DIFF));
}


TEST_F(CpuResourceHandlerTest, UpdateDiffSwitchingLatencyFails) {
  ContainerSpec spec;
  spec.mutable_cpu()->set_scheduling_latency(PREMIER);
  EXPECT_CALL(*mock_cpu_controller_, GetLatency())
      .WillOnce(Return(NORMAL));

  // Trying to update latency to PREMIER should fail.
  EXPECT_NOT_OK(handler_->Update(spec, Container::UPDATE_DIFF));
}

TEST_F(CpuResourceHandlerTest, UpdateDiffNoLatencySpecPasses) {
  ContainerSpec spec;
  // Add empty cpu spec.
  spec.mutable_cpu();
  EXPECT_CALL(*mock_cpu_controller_, GetLatency())
      .WillOnce(Return(NORMAL));

  EXPECT_OK(handler_->Update(spec, Container::UPDATE_DIFF));
}

TEST_F(CpuResourceHandlerTest, UpdateThroughputSucceeds) {
  ContainerSpec spec;
  spec.mutable_cpu()->set_limit(42);
  for (auto policy : kUpdatePolicy) {
    EXPECT_CALL(*mock_cpu_controller_, GetLatency())
        .WillOnce(Return(NORMAL));
    EXPECT_CALL(*mock_cpu_controller_, SetMilliCpus(42))
        .WillOnce(Return(Status::OK));

    if (policy == Container::UPDATE_REPLACE) {
      EXPECT_CALL(*mock_cpuset_controller_, InheritCpuMask())
          .WillOnce(Return(Status::OK));
      EXPECT_CALL(*mock_cpuset_controller_, InheritMemoryNodes())
          .WillOnce(Return(Status::OK));
    }

    EXPECT_OK(handler_->Update(spec, policy));
  }
}

TEST_F(CpuResourceHandlerTest, UpdateThroughputFails) {
  ContainerSpec spec;
  spec.mutable_cpu()->set_limit(42);

  for (auto policy : kUpdatePolicy) {
    EXPECT_CALL(*mock_cpu_controller_, GetLatency())
        .WillOnce(Return(NORMAL));
    EXPECT_CALL(*mock_cpu_controller_, SetMilliCpus(42))
        .WillRepeatedly(Return(Status::CANCELLED));

    if (policy == Container::UPDATE_REPLACE) {
      EXPECT_CALL(*mock_cpuset_controller_, InheritCpuMask())
          .WillRepeatedly(Return(Status::OK));
      EXPECT_CALL(*mock_cpuset_controller_, InheritMemoryNodes())
          .WillRepeatedly(Return(Status::OK));
    }

    EXPECT_NOT_OK(handler_->Update(spec, policy));
  }
}

TEST_F(CpuResourceHandlerTest, UpdateMaxThroughputSucceeds) {
  ContainerSpec spec;
  spec.mutable_cpu()->set_max_limit(42);

  for (auto policy : kUpdatePolicy) {
    EXPECT_CALL(*mock_cpu_controller_, GetLatency())
        .WillOnce(Return(NORMAL));
    EXPECT_CALL(*mock_cpu_controller_, SetMaxMilliCpus(42))
        .WillOnce(Return(Status::OK));

    if (policy == Container::UPDATE_REPLACE) {
      EXPECT_CALL(*mock_cpuset_controller_, InheritCpuMask())
          .WillOnce(Return(Status::OK));
      EXPECT_CALL(*mock_cpuset_controller_, InheritMemoryNodes())
          .WillOnce(Return(Status::OK));
    }

    EXPECT_OK(handler_->Update(spec, policy));
  }
}

TEST_F(CpuResourceHandlerTest, UpdateMaxThroughputFails) {
  ContainerSpec spec;
  spec.mutable_cpu()->set_max_limit(42);

  for (auto policy : kUpdatePolicy) {
    EXPECT_CALL(*mock_cpu_controller_, GetLatency())
        .WillOnce(Return(NORMAL));
    EXPECT_CALL(*mock_cpu_controller_, SetMaxMilliCpus(42))
        .WillRepeatedly(Return(Status::CANCELLED));

    if (policy == Container::UPDATE_REPLACE) {
      EXPECT_CALL(*mock_cpuset_controller_, InheritCpuMask())
          .WillRepeatedly(Return(Status::OK));
      EXPECT_CALL(*mock_cpuset_controller_, InheritMemoryNodes())
          .WillRepeatedly(Return(Status::OK));
    }

    EXPECT_NOT_OK(handler_->Update(spec, policy));
  }
}

TEST_F(CpuResourceHandlerTest, UpdateMaskSucceeds) {
  ContainerSpec spec;
  spec.mutable_cpu()->set_mask(42);

  for (auto policy : kUpdatePolicy) {
    EXPECT_CALL(*mock_cpu_controller_, GetLatency())
        .WillOnce(Return(NORMAL));
    // TODO(jnagal): Check actual value passed down with cpu_set_t
    // comparator.
    EXPECT_CALL(*mock_cpuset_controller_, SetCpuMask(testing::_))
         .WillOnce(Return(Status::OK));

    if (policy == Container::UPDATE_REPLACE) {
      EXPECT_CALL(*mock_cpuset_controller_, InheritMemoryNodes())
          .WillOnce(Return(Status::OK));
    }

    EXPECT_OK(handler_->Update(spec, policy));
  }
}

TEST_F(CpuResourceHandlerTest, UpdateMaskFails) {
  ContainerSpec spec;
  spec.mutable_cpu()->set_mask(42);

  for (auto policy : kUpdatePolicy) {
    EXPECT_CALL(*mock_cpu_controller_, GetLatency())
        .WillOnce(Return(NORMAL));
    EXPECT_CALL(*mock_cpuset_controller_, SetCpuMask(testing::_))
        .WillRepeatedly(Return(Status::CANCELLED));

    if (policy == Container::UPDATE_REPLACE) {
      EXPECT_CALL(*mock_cpuset_controller_, InheritMemoryNodes())
          .WillRepeatedly(Return(Status::OK));
    }

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
      .WillOnce(Return(NORMAL));
  EXPECT_CALL(*mock_cpuset_controller_, InheritCpuMask())
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_cpuset_controller_, InheritMemoryNodes())
      .WillOnce(Return(Status::OK));

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
  EXPECT_CALL(*mock_cpuset_controller_, InheritCpuMask())
      .WillRepeatedly(Return(Status::OK));
  EXPECT_CALL(*mock_cpuset_controller_, InheritMemoryNodes())
      .WillRepeatedly(Return(Status::OK));

  // changing latency is not allowed.
  EXPECT_NOT_OK(handler_->Update(spec, Container::UPDATE_REPLACE));
}

TEST_F(CpuResourceHandlerTest, UpdateReplaceSwitchingLatencyFails) {
  ContainerSpec spec;
  spec.mutable_cpu()->set_scheduling_latency(PREMIER);
  EXPECT_CALL(*mock_cpu_controller_, GetLatency()).WillOnce(Return(NORMAL));
  EXPECT_CALL(*mock_cpuset_controller_, InheritCpuMask())
      .WillRepeatedly(Return(Status::OK));
  EXPECT_CALL(*mock_cpuset_controller_, InheritMemoryNodes())
      .WillRepeatedly(Return(Status::OK));

  // Trying to update latency to PREMIER should fail.
  EXPECT_NOT_OK(handler_->Update(spec, Container::UPDATE_REPLACE));
}

TEST_F(CpuResourceHandlerTest, UpdateLatencyNotFoundAndNotSet) {
  ContainerSpec spec;
  spec.mutable_cpu()->set_max_limit(42);

  for (auto policy : kUpdatePolicy) {
    EXPECT_CALL(*mock_cpu_controller_, GetLatency())
        .WillOnce(Return(Status(::util::error::NOT_FOUND, "")));
    EXPECT_CALL(*mock_cpu_controller_, SetMaxMilliCpus(42))
        .WillRepeatedly(Return(Status::OK));

    if (policy == Container::UPDATE_REPLACE) {
      EXPECT_CALL(*mock_cpuset_controller_, InheritCpuMask())
          .WillOnce(Return(Status::OK));
      EXPECT_CALL(*mock_cpuset_controller_, InheritMemoryNodes())
          .WillOnce(Return(Status::OK));
    }

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
  EXPECT_EQ(::util::error::NOT_FOUND, statusor.status().error_code());
}

class CpuResourceHandlerSpecTest : public CpuResourceHandlerTest {
 public:
  virtual void SetUp() {
    CpuResourceHandlerTest::SetUp();
    EXPECT_CALL(*mock_cpu_controller_, GetMilliCpus())
        .WillRepeatedly(Return(StatusOr<int64>(123)));
    EXPECT_CALL(*mock_cpu_controller_, GetMaxMilliCpus())
        .WillRepeatedly(Return(StatusOr<int64>(456)));
  }
};

TEST_F(CpuResourceHandlerSpecTest, AllSucceed) {
  ContainerSpec spec;
  EXPECT_OK(handler_->Spec(&spec));
  EXPECT_EQ(123, spec.cpu().limit());
  EXPECT_EQ(456, spec.cpu().max_limit());
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

}  // namespace lmctfy
}  // namespace containers
