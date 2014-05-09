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

#include "lmctfy/resources/cgroup_resource_handler.h"

#include <map>
#include <memory>
#include <vector>

#include "system_api/kernel_api_mock.h"
#include "lmctfy/controllers/cgroup_controller_mock.h"
#include "lmctfy/controllers/cgroup_factory_mock.h"
#include "lmctfy/resource_handler_mock.h"
#include "include/lmctfy.pb.h"
#include "util/errors_test_util.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "util/task/status.h"

namespace containers {
namespace lmctfy {
class CgroupController;
}  // namespace lmctfy
}  // namespace containers

using ::system_api::KernelAPIMock;
using ::util::UnixGid;
using ::util::UnixUid;
using ::std::map;
using ::std::unique_ptr;
using ::std::vector;
using ::testing::DoAll;
#include "util/testing/equals_initialized_proto.h"
using ::testing::EqualsInitializedProto;
using ::testing::Invoke;
using ::testing::NotNull;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::StrictMock;
using ::testing::_;
using ::util::Status;
using ::util::StatusOr;
using ::util::error::INTERNAL;
using ::util::error::INVALID_ARGUMENT;
using ::util::error::NOT_FOUND;

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
  const vector<CgroupController *> &controllers() {
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

  EXPECT_CALL(*mock_handler, CreateResource(EqualsInitializedProto(spec)))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_handler,
              Update(EqualsInitializedProto(spec), Container::UPDATE_REPLACE))
      .WillOnce(Return(Status::OK));

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

TEST_F(CgroupResourceHandlerFactoryTest, CreateCreateFails) {
  ContainerSpec spec;
  MockResourceHandler *mock_handler =
      new StrictMockResourceHandler(kContainerName, RESOURCE_CPU);
  factory_->set_create_resource_handlers_status(mock_handler);

  EXPECT_CALL(*mock_handler, CreateResource(EqualsInitializedProto(spec)))
      .WillOnce(Return(Status::CANCELLED));
  EXPECT_CALL(*mock_handler,
              Update(EqualsInitializedProto(spec), Container::UPDATE_REPLACE))
      .WillRepeatedly(Return(Status::OK));

  EXPECT_EQ(Status::CANCELLED, factory_->Create(kContainerName, spec).status());
}

TEST_F(CgroupResourceHandlerFactoryTest, CreateUpdateFails) {
  ContainerSpec spec;
  MockResourceHandler *mock_handler =
      new StrictMockResourceHandler(kContainerName, RESOURCE_CPU);
  factory_->set_create_resource_handlers_status(mock_handler);

  EXPECT_CALL(*mock_handler, CreateResource(EqualsInitializedProto(spec)))
      .WillRepeatedly(Return(Status::OK));
  EXPECT_CALL(*mock_handler,
              Update(EqualsInitializedProto(spec), Container::UPDATE_REPLACE))
      .WillOnce(Return(Status::CANCELLED));

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

// Tests for Destroy().

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

// Tests for Enter().

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

// Tests for Delegate().

TEST_F(CgroupResourceHandlerTest, DelegateSuccess) {
  const UnixUid kUid(2);
  const UnixGid kGid(3);

  EXPECT_CALL(*mock_controller1_, Delegate(kUid, kGid))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_controller2_, Delegate(kUid, kGid))
      .WillOnce(Return(Status::OK));

  EXPECT_OK(handler_->Delegate(kUid, kGid));
}

TEST_F(CgroupResourceHandlerTest, DelegateFails) {
  const UnixUid kUid(2);
  const UnixGid kGid(3);

  EXPECT_CALL(*mock_controller1_, Delegate(kUid, kGid))
      .WillRepeatedly(Return(Status::OK));
  EXPECT_CALL(*mock_controller2_, Delegate(kUid, kGid))
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED, handler_->Delegate(kUid, kGid));
}

// Tests for Create().

TEST_F(CgroupResourceHandlerTest, CreateSetsChildrenLimits) {
  EXPECT_CALL(*mock_controller1_, SetChildrenLimit(12))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_controller2_, SetChildrenLimit(12))
      .WillOnce(Return(Status::OK));

  ContainerSpec spec;
  spec.set_children_limit(12);

  EXPECT_OK(handler_->CreateResource(spec));
}

// Test for PopulateMachineSpec().

TEST_F(CgroupResourceHandlerTest, PopulateMachineSpecSuccess) {
  MachineSpec test_spec;

  EXPECT_CALL(*mock_controller1_, PopulateMachineSpec(&test_spec))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_controller2_, PopulateMachineSpec(&test_spec))
      .WillOnce(Return(Status::OK));

  EXPECT_OK(handler_->PopulateMachineSpec(&test_spec));
}

TEST_F(CgroupResourceHandlerTest, PopulateMachineSpecFailure) {
  MachineSpec test_spec;

  EXPECT_CALL(*mock_controller1_, PopulateMachineSpec(&test_spec))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_controller2_, PopulateMachineSpec(&test_spec))
      .WillOnce(Return(Status(INTERNAL, "Something somehow went wrong.")));

  EXPECT_ERROR_CODE(INTERNAL, handler_->PopulateMachineSpec(&test_spec));
}

class UpdateTemplateTestHelperMock : public CgroupResourceHandler {
 public:
  UpdateTemplateTestHelperMock(const string &container_name,
                               ResourceType resource_type,
                               const KernelApi *kernel,
                               const vector<CgroupController *> &controllers)
      : CgroupResourceHandler(container_name, resource_type, kernel,
                              controllers) {}
  MOCK_CONST_METHOD2(Stats, Status(Container::StatsType, ContainerStats *));
  MOCK_METHOD2(RegisterNotification, StatusOr<Container::NotificationId>(
      const EventSpec &, Callback1<Status> *callback));
  MOCK_CONST_METHOD1(Spec, Status(ContainerSpec *s));
  MOCK_METHOD1(DoUpdate, Status(const ContainerSpec &));
  MOCK_CONST_METHOD1(RecursiveFillDefaults,
                     void(ContainerSpec *s));
  MOCK_CONST_METHOD1(VerifyFullSpec, Status(const ContainerSpec &));
};

class CgroupResourceHandlerUpdateTest : public ::testing::Test {
 public:
  virtual void SetUp() {
    mock_kernel_.reset(new StrictMock<KernelAPIMock>());
    mock_controller1_ =
        new StrictMockCgroupController(CGROUP_CPU, kMountPoint, true);
    mock_controller2_ =
        new StrictMockCgroupController(CGROUP_CPUACCT, kMountPoint, true);
    handler_.reset(new UpdateTemplateTestHelperMock(
        kContainerName, RESOURCE_CPU, mock_kernel_.get(),
        {mock_controller1_, mock_controller2_}));
  }

 protected:
  MockCgroupController *mock_controller1_;
  MockCgroupController *mock_controller2_;
  unique_ptr<KernelAPIMock> mock_kernel_;
  unique_ptr<UpdateTemplateTestHelperMock> handler_;
};

TEST_F(CgroupResourceHandlerUpdateTest, UpdateDiff) {
  ContainerSpec spec;

  ContainerSpec state_spec;
  state_spec.mutable_filesystem()->set_fd_limit(10);

  EXPECT_CALL(*handler_, Spec(NotNull()))
      .WillOnce(Invoke([](ContainerSpec *spec) {
        if (!spec->filesystem().has_fd_limit())
          spec->mutable_filesystem()->set_fd_limit(10);
        return Status::OK;
      }));
  EXPECT_CALL(*handler_, VerifyFullSpec(EqualsInitializedProto(state_spec)))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*handler_, DoUpdate(EqualsInitializedProto(spec)))
      .WillOnce(Return(Status::OK));

  EXPECT_EQ(Status::OK,
            handler_->Update(spec, Container::UpdatePolicy::UPDATE_DIFF));
}

TEST_F(CgroupResourceHandlerUpdateTest, UpdateDiffSpecError) {
  ContainerSpec spec;

  EXPECT_CALL(*handler_, Spec(NotNull()))
      .WillOnce(Return(Status(NOT_FOUND, "")));

  EXPECT_ERROR_CODE(NOT_FOUND,
                    handler_->Update(spec,
                                     Container::UpdatePolicy::UPDATE_DIFF));
}

TEST_F(CgroupResourceHandlerUpdateTest, UpdateDiffVerifyError) {
  ContainerSpec spec;

  ContainerSpec state_spec;
  state_spec.mutable_filesystem()->set_fd_limit(10);

  EXPECT_CALL(*handler_, Spec(NotNull()))
      .WillOnce(Invoke([](ContainerSpec *spec) {
        if (!spec->filesystem().has_fd_limit())
          spec->mutable_filesystem()->set_fd_limit(10);
        return Status::OK;
      }));
  EXPECT_CALL(*handler_, VerifyFullSpec(EqualsInitializedProto(state_spec)))
      .WillOnce(Return(Status(INVALID_ARGUMENT, "")));

  EXPECT_ERROR_CODE(INVALID_ARGUMENT,
                    handler_->Update(spec,
                                     Container::UpdatePolicy::UPDATE_DIFF));
}

TEST_F(CgroupResourceHandlerUpdateTest, UpdateDiffUpdateError) {
  ContainerSpec spec;

  ContainerSpec state_spec;
  state_spec.mutable_filesystem()->set_fd_limit(10);

  EXPECT_CALL(*handler_, Spec(NotNull()))
      .WillOnce(Invoke([](ContainerSpec *spec) {
        if (!spec->filesystem().has_fd_limit())
          spec->mutable_filesystem()->set_fd_limit(10);
        return Status::OK;
      }));
  EXPECT_CALL(*handler_, VerifyFullSpec(EqualsInitializedProto(state_spec)))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*handler_, DoUpdate(EqualsInitializedProto(spec)))
      .WillOnce(Return(Status(NOT_FOUND, "")));

  EXPECT_ERROR_CODE(NOT_FOUND,
                    handler_->Update(spec,
                                     Container::UpdatePolicy::UPDATE_DIFF));
}

TEST_F(CgroupResourceHandlerUpdateTest, UpdateReplace) {
  ContainerSpec spec;
  spec.mutable_memory()->set_limit(100);

  ContainerSpec spec_with_defaults;
  spec_with_defaults.CopyFrom(spec);
  spec_with_defaults.mutable_memory()->set_reservation(33);

  EXPECT_CALL(*handler_, RecursiveFillDefaults(NotNull()))
      .WillOnce(Invoke([](ContainerSpec *spec) {
        if (!spec->mutable_memory()->has_reservation())
          spec->mutable_memory()->set_reservation(33);
      }));
  EXPECT_CALL(*handler_, Spec(NotNull()))
      .WillOnce(Invoke([](ContainerSpec *spec) {
        if (!spec->mutable_memory()->has_limit())
          spec->mutable_memory()->set_limit(50);
        if (!spec->mutable_memory()->has_reservation())
          spec->mutable_memory()->set_reservation(1);
        return Status::OK;
      }));
  EXPECT_CALL(*handler_,
              VerifyFullSpec(EqualsInitializedProto(spec_with_defaults)))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*handler_, DoUpdate(EqualsInitializedProto(spec_with_defaults)))
      .WillOnce(Return(Status::OK));
  EXPECT_OK(handler_->Update(spec, Container::UpdatePolicy::UPDATE_REPLACE));
}

TEST_F(CgroupResourceHandlerUpdateTest, UpdateReplaceVerifyError) {
  ContainerSpec spec;
  spec.mutable_memory()->set_limit(100);

  ContainerSpec spec_with_defaults;
  spec_with_defaults.CopyFrom(spec);
  spec_with_defaults.mutable_memory()->set_reservation(33);

  EXPECT_CALL(*handler_, RecursiveFillDefaults(_))
      .WillOnce(Invoke([](ContainerSpec *spec) {
        if (!spec->mutable_memory()->has_reservation())
          spec->mutable_memory()->set_reservation(33);
      }));
  EXPECT_CALL(*handler_, Spec(NotNull()))
      .WillOnce(Invoke([](ContainerSpec *spec) {
        if (!spec->mutable_memory()->has_limit())
          spec->mutable_memory()->set_limit(50);
        if (!spec->mutable_memory()->has_reservation())
          spec->mutable_memory()->set_reservation(1);
        return Status::OK;
      }));
  EXPECT_CALL(*handler_,
              VerifyFullSpec(EqualsInitializedProto(spec_with_defaults)))
      .WillOnce(Return(Status(INVALID_ARGUMENT, "")));
  EXPECT_ERROR_CODE(INVALID_ARGUMENT,
                    handler_->Update(spec,
                                     Container::UpdatePolicy::UPDATE_REPLACE));
}

TEST_F(CgroupResourceHandlerUpdateTest, UpdateReplaceUpdateError) {
  ContainerSpec spec;
  spec.mutable_memory()->set_limit(100);

  ContainerSpec spec_with_defaults;
  spec_with_defaults.CopyFrom(spec);
  spec_with_defaults.mutable_memory()->set_reservation(33);

  EXPECT_CALL(*handler_, RecursiveFillDefaults(_))
      .WillOnce(Invoke([](ContainerSpec *spec) {
        if (!spec->mutable_memory()->has_reservation())
          spec->mutable_memory()->set_reservation(33);
      }));
  EXPECT_CALL(*handler_, Spec(NotNull()))
      .WillOnce(Invoke([](ContainerSpec *spec) {
        if (!spec->mutable_memory()->has_limit())
          spec->mutable_memory()->set_limit(50);
        if (!spec->mutable_memory()->has_reservation())
          spec->mutable_memory()->set_reservation(1);
        return Status::OK;
      }));
  EXPECT_CALL(*handler_,
              VerifyFullSpec(EqualsInitializedProto(spec_with_defaults)))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*handler_, DoUpdate(EqualsInitializedProto(spec_with_defaults)))
      .WillOnce(Return(Status(NOT_FOUND, "")));
  EXPECT_ERROR_CODE(NOT_FOUND,
                    handler_->Update(spec,
                                     Container::UpdatePolicy::UPDATE_REPLACE));
}

}  // namespace
}  // namespace lmctfy
}  // namespace containers
