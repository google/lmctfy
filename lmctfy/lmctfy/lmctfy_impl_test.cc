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

#include "lmctfy/lmctfy_impl.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/callback.h"
#include "gflags/gflags.h"
#include "base/logging.h"
#include "system_api/kernel_api_mock.h"
#include "system_api/kernel_api_test_util.h"
#include "file/base/path.h"
#include "lmctfy/active_notifications.h"
#include "lmctfy/controllers/cgroup_factory_mock.h"
#include "lmctfy/controllers/eventfd_notifications_mock.h"
#include "lmctfy/controllers/freezer_controller_mock.h"
#include "lmctfy/general_resource_handler_mock.h"
#include "lmctfy/namespace_handler_mock.h"
#include "lmctfy/resource_handler_mock.h"
#include "lmctfy/tasks_handler_mock.h"
#include "include/lmctfy.pb.h"
#include "include/lmctfy_mock.h"
#include "util/safe_types/unix_gid.h"
#include "util/safe_types/unix_uid.h"
#include "util/errors_test_util.h"
#include "util/file_lines_test_util.h"
#include "system_api/libc_fs_api_test_util.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "util/gtl/stl_util.h"
#include "util/task/codes.pb.h"

namespace containers {
namespace lmctfy {
class CgroupFactory;
class TasksHandler;
}  // namespace lmctfy
}  // namespace containers

DECLARE_int32(lmctfy_ms_delay_between_kills);

using ::system_api::MockKernelApiOverride;
using ::file::JoinPath;
using ::util::FileLinesTestUtil;
using ::system_api::MockLibcFsApiOverride;
using ::util::UnixGid;
using ::util::UnixGidValue;
using ::util::UnixUid;
using ::util::UnixUidValue;
using ::std::make_pair;
using ::std::map;
using ::std::sort;
using ::std::unique_ptr;
using ::std::unique_ptr;
using ::std::vector;
using ::testing::AtLeast;
using ::testing::AtMost;
using ::testing::Cardinality;
using ::testing::ContainerEq;
using ::testing::Contains;
using ::testing::ContainsRegex;
using ::testing::DoAll;
#include "util/testing/equals_initialized_proto.h"
using ::testing::EqualsInitializedProto;
using ::testing::Exactly;
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::NotNull;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::StrEq;
using ::testing::StrEq;
using ::testing::StrictMock;
using ::testing::_;
using ::util::error::INTERNAL;
using ::util::Status;
using ::util::StatusOr;
using ::util::error::FAILED_PRECONDITION;
using ::util::error::NOT_FOUND;
using ::util::error::UNIMPLEMENTED;

namespace containers {
namespace lmctfy {

typedef ::system_api::KernelAPIMock MockKernelApi;

class MockContainerApiImpl : public ContainerApiImpl {
 public:
  MockContainerApiImpl(MockTasksHandlerFactory *mock_tasks_handler_factory,
                   CgroupFactory *cgroup_factory,
                   const vector<ResourceHandlerFactory *> &resource_factories,
                   const MockKernelApi *mock_kernel,
                   ActiveNotifications *active_notifications,
                   NamespaceHandlerFactory *namespace_handler_factory,
                   EventFdNotifications *eventfd_notifications)
      : ContainerApiImpl(mock_tasks_handler_factory,
                     unique_ptr<CgroupFactory>(cgroup_factory),
                     resource_factories, mock_kernel, active_notifications,
                     namespace_handler_factory, eventfd_notifications,
                     unique_ptr<FreezerControllerFactory>(nullptr)) {}

  MOCK_CONST_METHOD1(Get, StatusOr<Container *>(StringPiece container_name));
  MOCK_CONST_METHOD2(Create, StatusOr<Container *>(StringPiece container_name,
                                                   const ContainerSpec &spec));
  MOCK_CONST_METHOD1(Destroy, Status(Container *container));
  MOCK_CONST_METHOD1(Exists, bool(const string &container_name));
  MOCK_CONST_METHOD1(Detect, StatusOr<string>(pid_t pid));
  MOCK_CONST_METHOD1(InitMachine, Status(const InitSpec &spec));
};

class ContainerApiImplTest : public ::testing::Test {
 public:
  ContainerApiImplTest() : mock_file_lines_(&mock_libc_fs_api_) {}

  void SetUp() override {
    mock_handler_factory1_ = new NiceMockResourceHandlerFactory(RESOURCE_CPU);
    mock_handler_factory2_ = new NiceMockResourceHandlerFactory(
        RESOURCE_MEMORY);
    mock_handler_factory3_ = new NiceMockResourceHandlerFactory(
        RESOURCE_FILESYSTEM);
    mock_handler_factory4_ = new NiceMockResourceHandlerFactory(
        RESOURCE_DEVICE);
    mock_namespace_handler_factory_ = new NiceMockNamespaceHandlerFactory();

    EXPECT_CALL(*mock_namespace_handler_factory_,
                CreateNamespaceHandler(_, _, _))
        .WillRepeatedly(Invoke([](const string &name,
                                  const ContainerSpec &spec,
                                  const MachineSpec &machine_spec) {
          return new StrictMockNamespaceHandler(name, RESOURCE_VIRTUALHOST);
        }));
    resource_factories_.push_back(mock_handler_factory1_);
    resource_factories_.push_back(mock_handler_factory2_);
    resource_factories_.push_back(mock_handler_factory3_);
    resource_factories_.push_back(mock_handler_factory4_);

    active_notifications_ = new ActiveNotifications();
    mock_eventfd_notifications_ = MockEventFdNotifications::NewStrict();
    mock_tasks_handler_factory_ = new StrictMockTasksHandlerFactory();
    mock_cgroup_factory_ = new StrictMockCgroupFactory();
    mock_freezer_controller_factory_ =
        new MockFreezerControllerFactory(mock_cgroup_factory_);

    lmctfy_.reset(
        new ContainerApiImpl(
            mock_tasks_handler_factory_,
            unique_ptr<CgroupFactory>(mock_cgroup_factory_),
            resource_factories_, &mock_kernel_.Mock(),
            active_notifications_, mock_namespace_handler_factory_,
            mock_eventfd_notifications_,
            unique_ptr<FreezerControllerFactory>(
                mock_freezer_controller_factory_)));
  }

  void ExpectCgroupFactoryMountCall(const CgroupMount &cgroup) {
    EXPECT_CALL(*mock_cgroup_factory_, Mount(EqualsInitializedProto(cgroup)))
        .WillOnce(Return(Status::OK));
  }

  void ExpectCgroupFactoryMountFailure(const CgroupMount &cgroup,
                                       const Status &error) {
    EXPECT_CALL(*mock_cgroup_factory_, Mount(EqualsInitializedProto(cgroup)))
        .WillOnce(Return(error));
  }

  void ExpectResourceFactoriesInitMachineCall(const InitSpec &init_spec) {
    for (const auto rfactory : resource_factories_) {
      EXPECT_CALL(
          *reinterpret_cast<MockResourceHandlerFactory *>(rfactory),
          InitMachine(EqualsInitializedProto(init_spec)))
          .WillOnce(Return(Status::OK));
    }
  }

  void ExpectNamespaceHandlerFactoryInitMachine(const InitSpec &init_spec) {
    EXPECT_CALL(*mock_namespace_handler_factory_,
                InitMachine(EqualsInitializedProto(init_spec)))
        .WillOnce(Return(Status::OK));
  }

  void ExpectNamespaceHandlerFactoryInitMachineFails(
      const InitSpec &init_spec) {
    EXPECT_CALL(*mock_namespace_handler_factory_,
                InitMachine(EqualsInitializedProto(init_spec)))
        .WillOnce(Return(Status(INTERNAL, "blah")));
  }

  StatusOr<string> CallResolveContainerName(const string &container_name) {
    return lmctfy_->ResolveContainerName(container_name);
  }

 protected:
  unique_ptr<ContainerApiImpl> lmctfy_;
  MockTasksHandlerFactory *mock_tasks_handler_factory_;
  MockCgroupFactory *mock_cgroup_factory_;
  MockResourceHandlerFactory *mock_handler_factory1_;
  MockResourceHandlerFactory *mock_handler_factory2_;
  MockResourceHandlerFactory *mock_handler_factory3_;
  MockResourceHandlerFactory *mock_handler_factory4_;
  ActiveNotifications *active_notifications_;
  MockNamespaceHandlerFactory *mock_namespace_handler_factory_;
  MockEventFdNotifications *mock_eventfd_notifications_;
  MockFreezerControllerFactory *mock_freezer_controller_factory_;

  MockLibcFsApiOverride mock_libc_fs_api_;
  FileLinesTestUtil mock_file_lines_;
  MockKernelApiOverride mock_kernel_;
  vector<ResourceHandlerFactory *> resource_factories_;
};

// Tests for ContainerApiImpl::NewContainerApiImpl()

TEST_F(ContainerApiImplTest, NewContainerApiImpl) {
  unique_ptr<MockCgroupFactory> mock_cgroup_factory(
      new StrictMockCgroupFactory());

  EXPECT_CALL(*mock_cgroup_factory, OwnsCgroup(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cgroup_factory, IsMounted(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(mock_kernel_.Mock(), EpollCreate(_)).WillRepeatedly(Return(0));
  EXPECT_CALL(mock_libc_fs_api_.Mock(), LStat(_, _)).WillRepeatedly(Return(-1));

  StatusOr<ContainerApi *> statusor =
      ContainerApiImpl::NewContainerApiImpl(move(mock_cgroup_factory),
                                    &mock_kernel_.Mock());
  ASSERT_OK(statusor);
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  delete statusor.ValueOrDie();
}

TEST_F(ContainerApiImplTest, NewContainerApiImplNoJobHierarchy) {
  unique_ptr<MockCgroupFactory> mock_cgroup_factory(
      new StrictMockCgroupFactory());

  EXPECT_CALL(*mock_cgroup_factory, OwnsCgroup(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cgroup_factory, IsMounted(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cgroup_factory, IsMounted(CGROUP_JOB))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(mock_kernel_.Mock(), EpollCreate(_)).WillRepeatedly(Return(0));
  EXPECT_CALL(mock_libc_fs_api_.Mock(), LStat(_, _)).WillRepeatedly(Return(-1));

  StatusOr<ContainerApi *> statusor =
      ContainerApiImpl::NewContainerApiImpl(move(mock_cgroup_factory),
                                    &mock_kernel_.Mock());
  ASSERT_OK(statusor);
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  delete statusor.ValueOrDie();
}

TEST_F(ContainerApiImplTest, NewContainerApiImpl_NoFreezer) {
  unique_ptr<MockCgroupFactory> mock_cgroup_factory(
      new StrictMockCgroupFactory());

  EXPECT_CALL(*mock_cgroup_factory, OwnsCgroup(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cgroup_factory, IsMounted(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cgroup_factory, IsMounted(CGROUP_FREEZER))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(mock_kernel_.Mock(), EpollCreate(_)).WillRepeatedly(Return(0));
  EXPECT_CALL(mock_libc_fs_api_.Mock(), LStat(_, _)).WillRepeatedly(Return(-1));

  StatusOr<ContainerApi *> statusor =
      ContainerApiImpl::NewContainerApiImpl(move(mock_cgroup_factory),
                                    &mock_kernel_.Mock());
  ASSERT_OK(statusor);
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  delete statusor.ValueOrDie();
}

// Tests for ContainerApiImpl::InitMachine()

TEST_F(ContainerApiImplTest, InitMachineSuccess) {
  CgroupMount mount1;
  CgroupMount mount2;
  CgroupMount mount3;
  CgroupMount mount4;
  CgroupMount mount5;
  CgroupMount mount6;
  CgroupMount mount7;

  mount1.set_mount_path("/dev/cgroup/memory");
  mount1.add_hierarchy(CGROUP_MEMORY);
  mount2.set_mount_path("/dev/cgroup/cpu");
  mount2.add_hierarchy(CGROUP_CPU);
  mount2.add_hierarchy(CGROUP_CPUACCT);
  mount3.add_hierarchy(CGROUP_JOB);
  mount3.set_mount_path("/dev/cgroup/job");
  mount4.add_hierarchy(CGROUP_CPUSET);
  mount4.set_mount_path("/dev/cgroup/cpuset");
  mount5.add_hierarchy(CGROUP_PERF_EVENT);
  mount5.set_mount_path("/dev/cgroup/perf_event");
  mount6.add_hierarchy(CGROUP_FREEZER);
  mount6.set_mount_path("/dev/cgroup/freezer");
  mount7.add_hierarchy(CGROUP_DEVICE);
  mount7.set_mount_path("/dev/cgroup/devices");

  InitSpec spec;
  spec.add_cgroup_mount()->CopyFrom(mount1);
  ExpectCgroupFactoryMountCall(mount1);

  spec.add_cgroup_mount()->CopyFrom(mount2);
  ExpectCgroupFactoryMountCall(mount2);

  spec.add_cgroup_mount()->CopyFrom(mount3);
  ExpectCgroupFactoryMountCall(mount3);

  spec.add_cgroup_mount()->CopyFrom(mount4);
  ExpectCgroupFactoryMountCall(mount4);

  spec.add_cgroup_mount()->CopyFrom(mount5);
  ExpectCgroupFactoryMountCall(mount5);

  spec.add_cgroup_mount()->CopyFrom(mount6);
  ExpectCgroupFactoryMountCall(mount6);

  spec.add_cgroup_mount()->CopyFrom(mount7);
  ExpectCgroupFactoryMountCall(mount7);

  ExpectResourceFactoriesInitMachineCall(spec);
  ExpectNamespaceHandlerFactoryInitMachine(spec);

  EXPECT_OK(lmctfy_->InitMachine(spec));
}

TEST_F(ContainerApiImplTest, InitMachineMountFails) {
  CgroupMount mount1;
  mount1.set_mount_path("/dev/cgroup/memory");
  mount1.add_hierarchy(CGROUP_MEMORY);

  InitSpec spec;
  spec.add_cgroup_mount()->CopyFrom(mount1);
  ExpectCgroupFactoryMountFailure(mount1, Status(INTERNAL, "blah"));

  EXPECT_NOT_OK(lmctfy_->InitMachine(spec));
}

TEST_F(ContainerApiImplTest, InitMachineResourceInitFails) {
  CgroupMount mount1;
  mount1.set_mount_path("/dev/cgroup/memory");
  mount1.add_hierarchy(CGROUP_MEMORY);

  InitSpec spec;
  spec.add_cgroup_mount()->CopyFrom(mount1);
  ExpectCgroupFactoryMountCall(mount1);

  EXPECT_CALL(
      *reinterpret_cast<MockResourceHandlerFactory *>(resource_factories_[0]),
      InitMachine(EqualsInitializedProto(spec)))
      .WillOnce(Return(Status(INTERNAL, "blah")));

  EXPECT_ERROR_CODE(INTERNAL, lmctfy_->InitMachine(spec));
}

TEST_F(ContainerApiImplTest, InitMachineNamespaceFactoryInitMachineFails) {
  CgroupMount mount1;
  mount1.set_mount_path("/dev/cgroup/memory");
  mount1.add_hierarchy(CGROUP_MEMORY);

  InitSpec spec;
  spec.add_cgroup_mount()->CopyFrom(mount1);
  ExpectCgroupFactoryMountCall(mount1);
  ExpectResourceFactoriesInitMachineCall(spec);
  ExpectNamespaceHandlerFactoryInitMachineFails(spec);

  EXPECT_ERROR_CODE(INTERNAL, lmctfy_->InitMachine(spec));
}

// Tests for Get()

TEST_F(ContainerApiImplTest, GetSuccess) {
  const string kName = "/test";

  EXPECT_CALL(*mock_tasks_handler_factory_, Exists(kName))
      .WillRepeatedly(Return(true));

  // Get the Freezer handler
  EXPECT_CALL(*mock_freezer_controller_factory_, Get(kName))
      .WillOnce(Return(new StrictMockFreezerController()));

  // Get the Tasks handler
  EXPECT_CALL(*mock_tasks_handler_factory_, Get(kName))
      .WillRepeatedly(Return(StatusOr<TasksHandler *>(
          new StrictMockTasksHandler(kName))));

  StatusOr<Container *> status = lmctfy_->Get(kName);
  ASSERT_TRUE(status.ok());
  EXPECT_NE(static_cast<Container *>(NULL), status.ValueOrDie());
  delete status.ValueOrDie();
}

TEST_F(ContainerApiImplTest, GetContainerDoesNotExist) {
  const string kName = "/test";

  EXPECT_CALL(*mock_tasks_handler_factory_, Exists(kName))
      .WillRepeatedly(Return(false));

  StatusOr<Container *> status = lmctfy_->Get(kName);
  EXPECT_EQ(::util::error::NOT_FOUND, status.status().error_code());
}

TEST_F(ContainerApiImplTest, GetErrorGettingTasksHandler) {
  const string kName = "/test";

  EXPECT_CALL(*mock_tasks_handler_factory_, Exists(kName))
      .WillRepeatedly(Return(true));

  EXPECT_CALL(*mock_freezer_controller_factory_, Get(kName))
      .WillOnce(Return(new StrictMockFreezerController()));

  EXPECT_CALL(*mock_tasks_handler_factory_, Get(kName))
      .WillRepeatedly(Return(StatusOr<TasksHandler *>(Status::CANCELLED)));

  StatusOr<Container *> status = lmctfy_->Get(kName);
  EXPECT_EQ(::util::error::CANCELLED, status.status().error_code());
}

TEST_F(ContainerApiImplTest, GetBadContainerName) {
  StatusOr<Container *> status = lmctfy_->Get("*");
  EXPECT_EQ(::util::error::INVALID_ARGUMENT, status.status().error_code());
}

TEST_F(ContainerApiImplTest, GetErrorGettingFreezerController) {
  const string kName = "/test";

  EXPECT_CALL(*mock_freezer_controller_factory_, Get(kName))
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_CALL(*mock_tasks_handler_factory_, Exists(kName))
      .WillRepeatedly(Return(true));

  EXPECT_ERROR_CODE(::util::error::CANCELLED,  lmctfy_->Get(kName));
}

TEST_F(ContainerApiImplTest, GetFailure_MissingFreezerEntry) {
  const string kName = "/test";

  EXPECT_CALL(*mock_tasks_handler_factory_, Exists(kName))
      .WillRepeatedly(Return(true));

  // Get the Freezer handler
  EXPECT_CALL(*mock_freezer_controller_factory_, Get(kName))
      .WillOnce(Return(Status(::util::error::NOT_FOUND,
                              "freezer entry missing")));

  // Get the Tasks handler
  EXPECT_CALL(*mock_tasks_handler_factory_, Get(kName))
      .WillRepeatedly(Return(StatusOr<TasksHandler *>(
          new StrictMockTasksHandler(kName))));

  EXPECT_ERROR_CODE(::util::error::NOT_FOUND, lmctfy_->Get(kName));
}

// Tests for Create()

TEST_F(ContainerApiImplTest, CreateSuccess) {
  const string kName = "/test";

  ContainerSpec spec;
  spec.mutable_cpu();
  spec.mutable_memory();
  spec.mutable_filesystem();

  EXPECT_CALL(*mock_freezer_controller_factory_, Create(kName))
      .WillRepeatedly(Return(new StrictMockFreezerController()));

  EXPECT_CALL(*mock_tasks_handler_factory_, Exists(kName))
      .WillRepeatedly(Return(false));

  EXPECT_CALL(*mock_handler_factory1_, Create(kName, _))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          new StrictMockResourceHandler(kName, RESOURCE_CPU))));
  EXPECT_CALL(*mock_handler_factory2_, Create(kName, _))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          new StrictMockResourceHandler(kName, RESOURCE_MEMORY))));
  EXPECT_CALL(*mock_handler_factory3_, Create(kName, _))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          new StrictMockResourceHandler(kName, RESOURCE_FILESYSTEM))));
  EXPECT_CALL(*mock_tasks_handler_factory_,
              Create(kName, EqualsInitializedProto(spec)))
      .WillRepeatedly(
           Return(StatusOr<TasksHandler *>(new StrictMockTasksHandler(kName))));

  StatusOr<Container *> status = lmctfy_->Create(kName, spec);
  ASSERT_TRUE(status.ok());
  EXPECT_NE(static_cast<Container *>(NULL), status.ValueOrDie());
  delete status.ValueOrDie();
}

TEST_F(ContainerApiImplTest, CreateSuccessWithAllResources) {
  const string kName = "/test";

  ContainerSpec spec;
  spec.mutable_cpu();
  spec.mutable_memory();
  spec.mutable_blockio();
  spec.mutable_network();
  spec.mutable_monitoring();
  spec.mutable_filesystem();

  MockResourceHandlerFactory *cpu_handler =
      new StrictMockResourceHandlerFactory(RESOURCE_CPU);
  MockResourceHandlerFactory *memory_handler =
      new StrictMockResourceHandlerFactory(RESOURCE_MEMORY);
  MockResourceHandlerFactory *blockio_handler =
      new StrictMockResourceHandlerFactory(RESOURCE_BLOCKIO);
  MockResourceHandlerFactory *network_handler =
      new StrictMockResourceHandlerFactory(RESOURCE_NETWORK);
  MockResourceHandlerFactory *monitoring_handler =
      new StrictMockResourceHandlerFactory(RESOURCE_MONITORING);
  MockResourceHandlerFactory *filesystem_handler =
      new StrictMockResourceHandlerFactory(RESOURCE_FILESYSTEM);
  MockNamespaceHandlerFactory *namespace_handler =
      new StrictMockNamespaceHandlerFactory();

  const vector<ResourceHandlerFactory *> resource_factories = {
    cpu_handler, memory_handler, blockio_handler, network_handler,
    monitoring_handler, filesystem_handler,
  };

  MockTasksHandlerFactory *mock_tasks_handler_factory =
      new StrictMockTasksHandlerFactory();
  unique_ptr<MockCgroupFactory> mock_cgroup_factory(
      new StrictMockCgroupFactory());

  MockFreezerControllerFactory *freezer_controller_factory =
      new MockFreezerControllerFactory(mock_cgroup_factory.get());

  EXPECT_CALL(*freezer_controller_factory, Create(kName))
      .WillRepeatedly(Return(new StrictMockFreezerController()));

  lmctfy_.reset(new ContainerApiImpl(
      mock_tasks_handler_factory, move(mock_cgroup_factory), resource_factories,
      new StrictMock<MockKernelApi>(), new ActiveNotifications(),
      namespace_handler,
      MockEventFdNotifications::NewStrict(),
      unique_ptr<FreezerControllerFactory>(freezer_controller_factory)));

  EXPECT_CALL(*mock_tasks_handler_factory, Exists(kName))
      .WillRepeatedly(Return(false));

  EXPECT_CALL(*cpu_handler, Create(kName, _))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          new StrictMockResourceHandler(kName, RESOURCE_CPU))));
  EXPECT_CALL(*memory_handler, Create(kName, _))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          new StrictMockResourceHandler(kName, RESOURCE_MEMORY))));
  EXPECT_CALL(*blockio_handler, Create(kName, _))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          new StrictMockResourceHandler(kName, RESOURCE_BLOCKIO))));
  EXPECT_CALL(*network_handler, Create(kName, _))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          new StrictMockResourceHandler(kName, RESOURCE_NETWORK))));
  EXPECT_CALL(*monitoring_handler, Create(kName, _))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          new StrictMockResourceHandler(kName, RESOURCE_MONITORING))));
  EXPECT_CALL(*filesystem_handler, Create(kName, _))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          new StrictMockResourceHandler(kName, RESOURCE_FILESYSTEM))));
  EXPECT_CALL(*namespace_handler, CreateNamespaceHandler(kName, _, _))
      .WillRepeatedly(Return(StatusOr<NamespaceHandler *>(
          new StrictMockNamespaceHandler(kName, RESOURCE_VIRTUALHOST))));
  EXPECT_CALL(*mock_tasks_handler_factory,
              Create(kName, EqualsInitializedProto(spec)))
      .WillRepeatedly(
           Return(StatusOr<TasksHandler *>(new StrictMockTasksHandler(kName))));

  StatusOr<Container *> status = lmctfy_->Create(kName, spec);
  ASSERT_TRUE(status.ok());
  EXPECT_NE(static_cast<Container *>(NULL), status.ValueOrDie());
  delete status.ValueOrDie();
}

Status PopulateMachineSpecCB(CgroupHierarchy hierarchy, const string &root,
                             MachineSpec *spec) {
  auto *virt_root = spec->mutable_virtual_root()->add_cgroup_virtual_root();
  virt_root->set_root(root);
  virt_root->set_hierarchy(hierarchy);
  return Status::OK;
}

TEST_F(ContainerApiImplTest, CreateWithVirtualHost) {
  const string kName = "/test";

  ContainerSpec spec;
  spec.mutable_cpu();
  spec.mutable_memory();
  spec.mutable_filesystem();
  spec.mutable_virtual_host();

  MachineSpec machine;
  auto *virt_root1 = machine.mutable_virtual_root()->add_cgroup_virtual_root();
  virt_root1->set_root(kName);
  virt_root1->set_hierarchy(CGROUP_CPU);
  auto *virt_root2 = machine.mutable_virtual_root()->add_cgroup_virtual_root();
  virt_root2->set_root(kName);
  virt_root2->set_hierarchy(CGROUP_MEMORY);
  auto *virt_root3 = machine.mutable_virtual_root()->add_cgroup_virtual_root();
  virt_root3->set_root(kName);
  virt_root3->set_hierarchy(CGROUP_BLOCKIO);
  auto *virt_root4 = machine.mutable_virtual_root()->add_cgroup_virtual_root();
  virt_root4->set_root(kName);
  virt_root4->set_hierarchy(CGROUP_DEVICE);

  StrictMockFreezerController *freezer_cont = new StrictMockFreezerController();
  StrictMockTasksHandler *task_hand = new StrictMockTasksHandler(kName);
  StrictMockResourceHandler *rhand1 =
      new StrictMockResourceHandler(kName, RESOURCE_CPU);
  StrictMockResourceHandler *rhand2 =
      new StrictMockResourceHandler(kName, RESOURCE_MEMORY);
  StrictMockResourceHandler *rhand3 =
      new StrictMockResourceHandler(kName, RESOURCE_FILESYSTEM);
  StrictMockResourceHandler *rhand4 =
      new StrictMockResourceHandler(kName, RESOURCE_DEVICE);

  EXPECT_CALL(*freezer_cont, Enter(_)).WillRepeatedly(Return(Status::OK));
  EXPECT_CALL(*task_hand, TrackTasks(_)).WillRepeatedly(Return(Status::OK));
  EXPECT_CALL(*rhand1, Enter(_)).WillRepeatedly(Return(Status::OK));
  EXPECT_CALL(*rhand2, Enter(_)).WillRepeatedly(Return(Status::OK));
  EXPECT_CALL(*rhand3, Enter(_)).WillRepeatedly(Return(Status::OK));
  EXPECT_CALL(*rhand4, Enter(_)).WillRepeatedly(Return(Status::OK));

  EXPECT_CALL(*rhand1, PopulateMachineSpec(_)).WillRepeatedly(
      Invoke([kName](MachineSpec *spec) {
        return PopulateMachineSpecCB(CGROUP_CPU, kName, spec);
      }));
  EXPECT_CALL(*rhand2, PopulateMachineSpec(_)).WillRepeatedly(
      Invoke([kName](MachineSpec *spec) {
        return PopulateMachineSpecCB(CGROUP_MEMORY, kName, spec);
      }));
  EXPECT_CALL(*rhand3, PopulateMachineSpec(_)).WillRepeatedly(
      Invoke([kName](MachineSpec *spec) {
        return PopulateMachineSpecCB(CGROUP_BLOCKIO, kName, spec);
      }));
  EXPECT_CALL(*rhand4, PopulateMachineSpec(_)).WillRepeatedly(
      Invoke([kName](MachineSpec *spec) {
        return PopulateMachineSpecCB(CGROUP_DEVICE, kName, spec);
      }));

  EXPECT_CALL(*freezer_cont, PopulateMachineSpec(_))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*task_hand, PopulateMachineSpec(_))
      .WillOnce(Return(Status::OK));

  EXPECT_CALL(*mock_freezer_controller_factory_, Create(kName))
      .WillRepeatedly(Return(freezer_cont));
  EXPECT_CALL(*mock_tasks_handler_factory_, Exists(kName))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*mock_handler_factory1_, Create(kName, _))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          new StrictMockResourceHandler(kName, RESOURCE_CPU))));
  EXPECT_CALL(*mock_handler_factory2_, Create(kName, _))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          new StrictMockResourceHandler(kName, RESOURCE_MEMORY))));
  EXPECT_CALL(*mock_handler_factory3_, Create(kName, _))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          new StrictMockResourceHandler(kName, RESOURCE_FILESYSTEM))));
  EXPECT_CALL(*mock_tasks_handler_factory_,
              Create(kName, EqualsInitializedProto(spec)))
      .WillRepeatedly(
           Return(StatusOr<TasksHandler *>(task_hand)));

  EXPECT_CALL(*mock_handler_factory1_, Get(kName))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(rhand1)));
  EXPECT_CALL(*mock_handler_factory2_, Get(kName))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(rhand2)));
  EXPECT_CALL(*mock_handler_factory3_, Get(kName))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(rhand3)));
  EXPECT_CALL(*mock_handler_factory4_, Get(kName))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(rhand4)));

  EXPECT_CALL(*mock_cgroup_factory_, PopulateMachineSpec(NotNull()))
      .WillOnce(Return(Status::OK));

  EXPECT_CALL(*mock_namespace_handler_factory_, CreateNamespaceHandler(
      kName, _, EqualsInitializedProto(machine)))
      .WillOnce(Return(StatusOr<NamespaceHandler *>(
          new StrictMockNamespaceHandler(kName, RESOURCE_VIRTUALHOST))));

  StatusOr<Container *> status = lmctfy_->Create(kName, spec);
  ASSERT_OK(status);
  EXPECT_NE(nullptr, status.ValueOrDie());
  delete status.ValueOrDie();
}

TEST_F(ContainerApiImplTest, CreateOnlySpecCpuSuccess) {
  const string kParentName = "/";
  const string kName = "/test";

  ContainerSpec spec;
  spec.mutable_cpu();

  EXPECT_CALL(*mock_freezer_controller_factory_, Create(kName))
      .WillRepeatedly(Return(new StrictMockFreezerController()));

  EXPECT_CALL(*mock_tasks_handler_factory_, Exists(kName))
      .WillRepeatedly(Return(false));

  EXPECT_CALL(*mock_handler_factory1_, Create(kName, _))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          new StrictMockResourceHandler(kName, RESOURCE_CPU))));
  EXPECT_CALL(*mock_tasks_handler_factory_,
              Create(kName, EqualsInitializedProto(spec)))
      .WillRepeatedly(
           Return(StatusOr<TasksHandler *>(new StrictMockTasksHandler(kName))));

  // Memory and Global were not spec'd so they should not be created
  EXPECT_CALL(*mock_handler_factory2_, Create(_, _))
      .Times(0);
  EXPECT_CALL(*mock_handler_factory3_, Create(_, _))
      .Times(0);

  StatusOr<Container *> status = lmctfy_->Create(kName, spec);
  ASSERT_TRUE(status.ok());
  EXPECT_NE(static_cast<Container *>(NULL), status.ValueOrDie());
  delete status.ValueOrDie();
}

TEST_F(ContainerApiImplTest, CreateNoResourcesSpecified) {
  const string kParentName = "/";
  const string kName = "/test";

  ContainerSpec spec;

  EXPECT_CALL(*mock_freezer_controller_factory_, Create(kName))
      .WillRepeatedly(Return(new StrictMockFreezerController()));

  EXPECT_CALL(*mock_tasks_handler_factory_, Exists(kName))
      .WillRepeatedly(Return(false));

  // Resourced were not spec'd so they should not be created
  EXPECT_CALL(*mock_handler_factory1_, Create(_, _))
      .Times(0);
  EXPECT_CALL(*mock_handler_factory2_, Create(_, _))
      .Times(0);
  EXPECT_CALL(*mock_handler_factory3_, Create(_, _))
      .Times(0);

  // TasksHandler is always created
  EXPECT_CALL(*mock_tasks_handler_factory_,
              Create(kName, EqualsInitializedProto(spec)))
      .WillRepeatedly(
           Return(StatusOr<TasksHandler *>(new StrictMockTasksHandler(kName))));

  StatusOr<Container *> status = lmctfy_->Create(kName, spec);
  ASSERT_TRUE(status.ok());
  EXPECT_NE(static_cast<Container *>(NULL), status.ValueOrDie());
  delete status.ValueOrDie();
}

TEST_F(ContainerApiImplTest, CreateTasksHandlerCreationFails) {
  const string kName = "/test";

  ContainerSpec spec;
  spec.mutable_cpu();
  spec.mutable_memory();
  spec.mutable_network();

  unique_ptr<MockFreezerController> mock_freezer_controller(
      new StrictMockFreezerController());

  EXPECT_CALL(*mock_freezer_controller, Destroy())
      .WillOnce(Return(Status::OK));

  EXPECT_CALL(*mock_freezer_controller_factory_, Create(kName))
      .WillRepeatedly(Return(mock_freezer_controller.get()));

  EXPECT_CALL(*mock_tasks_handler_factory_, Exists(kName))
      .WillRepeatedly(Return(false));

  EXPECT_CALL(*mock_handler_factory1_, Create(kName, _))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          new StrictMockResourceHandler(kName, RESOURCE_CPU))));
  EXPECT_CALL(*mock_handler_factory2_, Create(kName, _))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          new StrictMockResourceHandler(kName, RESOURCE_MEMORY))));
  EXPECT_CALL(*mock_handler_factory3_, Create(kName, _))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          new StrictMockResourceHandler(kName, RESOURCE_FILESYSTEM))));

  EXPECT_CALL(*mock_tasks_handler_factory_,
              Create(kName, EqualsInitializedProto(spec)))
      .WillRepeatedly(Return(StatusOr<TasksHandler *>(Status::CANCELLED)));

  StatusOr<Container *> status = lmctfy_->Create(kName, spec);
  ASSERT_FALSE(status.ok());
  EXPECT_EQ(Status::CANCELLED, status.status());
}

TEST_F(ContainerApiImplTest, CreateFreezerControllerCreationFails) {
  const string kName = "/test";

  ContainerSpec spec;

  EXPECT_CALL(*mock_tasks_handler_factory_, Exists(kName))
      .WillRepeatedly(Return(false));

  EXPECT_CALL(*mock_freezer_controller_factory_, Create(kName))
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_CALL(*mock_tasks_handler_factory_,
              Create(kName, EqualsInitializedProto(spec)))
      .WillRepeatedly(Return(StatusOr<TasksHandler *>(Status::CANCELLED)));

  EXPECT_ERROR_CODE(::util::error::CANCELLED, lmctfy_->Create(kName, spec));
}

TEST_F(ContainerApiImplTest, CreateResourceCreationFails) {
  const string kParentName = "/";
  const string kName = "/test";

  ContainerSpec spec;
  spec.mutable_cpu();
  spec.mutable_filesystem();

  unique_ptr<MockFreezerController> mock_freezer_controller(
      new StrictMockFreezerController());

  EXPECT_CALL(*mock_freezer_controller_factory_, Create(kName))
      .WillOnce(Return(mock_freezer_controller.get()));

  EXPECT_CALL(*mock_tasks_handler_factory_, Exists(kName))
      .WillRepeatedly(Return(false));

  // Since CPU will be Destroy()ed, Destroy() will delete it.
  unique_ptr<MockResourceHandler> cpu_handler(
      new StrictMockResourceHandler(kName, RESOURCE_CPU));

  EXPECT_CALL(*mock_handler_factory1_, Create(kName, _))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(cpu_handler.get())));
  EXPECT_CALL(*mock_handler_factory2_, Get(kParentName))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          new StrictMockResourceHandler(kParentName, RESOURCE_MEMORY))));
  EXPECT_CALL(*mock_handler_factory3_, Create(kName, _))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(Status::CANCELLED)));
  unique_ptr<MockTasksHandler> mock_tasks_handler(
      new StrictMockTasksHandler(kName));
  EXPECT_CALL(*mock_tasks_handler_factory_,
              Create(kName, EqualsInitializedProto(spec)))
      .WillOnce(Return(mock_tasks_handler.get()));

  // The partially created resources will be destroyed
  EXPECT_CALL(*cpu_handler, Destroy())
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_tasks_handler, Destroy())
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_freezer_controller, Destroy())
      .WillOnce(Return(Status::OK));

  StatusOr<Container *> status = lmctfy_->Create(kName, spec);
  ASSERT_FALSE(status.ok());
  EXPECT_EQ(Status::CANCELLED, status.status());
}

TEST_F(ContainerApiImplTest, CreateNoContainerName) {
  ContainerSpec spec;
  spec.mutable_memory();

  StatusOr<Container *> status = lmctfy_->Create("", spec);
  EXPECT_EQ(::util::error::INVALID_ARGUMENT, status.status().error_code());
}

TEST_F(ContainerApiImplTest, CreateBadContainerName) {
  ContainerSpec spec;
  spec.mutable_memory();

  StatusOr<Container *> status = lmctfy_->Create("*", spec);
  EXPECT_EQ(::util::error::INVALID_ARGUMENT, status.status().error_code());
}

TEST_F(ContainerApiImplTest, CreateContainerAlreadyExists) {
  const string kName = "/test";

  EXPECT_CALL(*mock_tasks_handler_factory_, Exists(kName))
      .WillRepeatedly(Return(true));

  ContainerSpec spec;
  spec.mutable_memory();

  StatusOr<Container *> status = lmctfy_->Create(kName, spec);
  EXPECT_EQ(::util::error::ALREADY_EXISTS, status.status().error_code());
}

class ContainerApiImplDelegateTest : public ContainerApiImplTest {
 protected:
  void ExpectCreate(const string &kName, const ContainerSpec &spec) {
    mock_cpu_handler_ = new StrictMockResourceHandler(kName, RESOURCE_CPU);
    mock_tasks_handler_ = new StrictMockTasksHandler(kName);
    mock_freezer_controller_ = new StrictMockFreezerController();

    EXPECT_CALL(*mock_tasks_handler_factory_, Exists(kName))
        .WillRepeatedly(Return(false));

    EXPECT_CALL(*mock_handler_factory1_, Create(kName, _))
        .WillOnce(Return(mock_cpu_handler_));
    EXPECT_CALL(*mock_tasks_handler_factory_,
                Create(kName, EqualsInitializedProto(spec)))
        .WillOnce(Return(mock_tasks_handler_));
    EXPECT_CALL(*mock_freezer_controller_factory_, Create(kName))
        .WillOnce(Return(mock_freezer_controller_));
  }

  MockResourceHandler *mock_cpu_handler_;
  MockTasksHandler *mock_tasks_handler_;
  MockFreezerController *mock_freezer_controller_;
};

TEST_F(ContainerApiImplDelegateTest, CreateWithDelegatedUser) {
  const string kName = "/test";
  const UnixUid kUid(42);
  const UnixGid kGid(UnixGidValue::Invalid());

  ContainerSpec spec;
  spec.set_owner(kUid.value());
  spec.mutable_cpu();

  ExpectCreate(kName, spec);
  EXPECT_CALL(*mock_cpu_handler_, Delegate(kUid, kGid))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_tasks_handler_, Delegate(kUid, kGid))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_freezer_controller_, Delegate(kUid, kGid))
      .WillOnce(Return(Status::OK));

  StatusOr<Container *> status = lmctfy_->Create(kName, spec);
  ASSERT_OK(status);
  delete status.ValueOrDie();
}

TEST_F(ContainerApiImplDelegateTest, CreateWithDelegatedGid) {
  const string kName = "/test";
  const UnixUid kUid(UnixUidValue::Invalid());
  const UnixGid kGid(42);

  ContainerSpec spec;
  spec.set_owner_group(kGid.value());
  spec.mutable_cpu();

  ExpectCreate(kName, spec);
  EXPECT_CALL(*mock_cpu_handler_, Delegate(kUid, kGid))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_tasks_handler_, Delegate(kUid, kGid))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_freezer_controller_, Delegate(kUid, kGid))
      .WillOnce(Return(Status::OK));

  StatusOr<Container *> status = lmctfy_->Create(kName, spec);
  ASSERT_OK(status);
  delete status.ValueOrDie();
}

TEST_F(ContainerApiImplDelegateTest,
       CreateWithDelegatedUserDelegateTasksHandlerFails) {
  const string kName = "/test";
  const UnixUid kUid(42);
  const UnixGid kGid(UnixGidValue::Invalid());

  ContainerSpec spec;
  spec.set_owner(kUid.value());
  spec.mutable_cpu();

  ExpectCreate(kName, spec);

  EXPECT_CALL(*mock_freezer_controller_, Delegate(kUid, kGid))
      .WillOnce(Return(Status::OK));

  EXPECT_CALL(*mock_tasks_handler_, Delegate(kUid, kGid))
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_CALL(*mock_cpu_handler_, Destroy())
      .WillOnce(
          DoAll(Invoke([this]() { delete mock_cpu_handler_; }),
                Return(Status::OK)));
  EXPECT_CALL(*mock_tasks_handler_, Destroy())
      .WillOnce(
          DoAll(Invoke([this]() { delete mock_tasks_handler_; }),
                Return(Status::OK)));
  EXPECT_CALL(*mock_freezer_controller_, Destroy())
      .WillOnce(
          DoAll(Invoke([this]() { delete mock_freezer_controller_; }),
                Return(Status::OK)));

  EXPECT_ERROR_CODE(::util::error::CANCELLED, lmctfy_->Create(kName, spec));
}

TEST_F(ContainerApiImplDelegateTest,
       CreateWithDelegatedUserDelegateResourceHandlerFails) {
  const string kName = "/test";
  const UnixUid kUid(42);
  const UnixGid kGid(UnixGidValue::Invalid());

  ContainerSpec spec;
  spec.set_owner(kUid.value());
  spec.mutable_cpu();

  ExpectCreate(kName, spec);
  EXPECT_CALL(*mock_freezer_controller_, Delegate(kUid, kGid))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_tasks_handler_, Delegate(kUid, kGid))
      .WillRepeatedly(Return(Status::OK));
  EXPECT_CALL(*mock_cpu_handler_, Delegate(kUid, kGid))
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_CALL(*mock_cpu_handler_, Destroy())
      .WillOnce(
          DoAll(Invoke([this]() { delete mock_cpu_handler_; }),
                Return(Status::OK)));
  EXPECT_CALL(*mock_tasks_handler_, Destroy())
      .WillOnce(
          DoAll(Invoke([this]() { delete mock_tasks_handler_; }),
                Return(Status::OK)));
  EXPECT_CALL(*mock_freezer_controller_, Destroy())
      .WillOnce(
          DoAll(Invoke([this]() { delete mock_freezer_controller_; }),
                Return(Status::OK)));
  EXPECT_ERROR_CODE(::util::error::CANCELLED, lmctfy_->Create(kName, spec));
}

TEST_F(ContainerApiImplDelegateTest,
       CreateWithDelegatedUserFreezerControllerFails) {
  const string kName = "/test";
  const UnixUid kUid(42);
  const UnixGid kGid(UnixGidValue::Invalid());

  ContainerSpec spec;
  spec.set_owner(kUid.value());
  spec.mutable_cpu();

  ExpectCreate(kName, spec);
  EXPECT_CALL(*mock_freezer_controller_, Delegate(kUid, kGid))
      .WillOnce(Return(Status::CANCELLED));

  EXPECT_CALL(*mock_cpu_handler_, Destroy())
      .WillOnce(
          DoAll(Invoke([this]() { delete mock_cpu_handler_; }),
                Return(Status::OK)));
  EXPECT_CALL(*mock_tasks_handler_, Destroy())
      .WillOnce(
          DoAll(Invoke([this]() { delete mock_tasks_handler_; }),
                Return(Status::OK)));

  EXPECT_CALL(*mock_freezer_controller_, Destroy())
      .WillOnce(
          DoAll(Invoke([this]() { delete mock_freezer_controller_; }),
                Return(Status::OK)));

  EXPECT_ERROR_CODE(::util::error::CANCELLED, lmctfy_->Create(kName, spec));
}

// Tests for Destroy()

TEST_F(ContainerApiImplTest, DestroyNoSubcontainersSuccess) {
  MockContainer *mock_container = new StrictMock<MockContainer>("/test");

  EXPECT_CALL(*mock_container, Destroy())
      .WillRepeatedly(Return(Status::OK));
  EXPECT_CALL(*mock_container, ListSubcontainers(Container::LIST_RECURSIVE))
      .WillRepeatedly(
          Return(StatusOr<vector<Container *>>(vector<Container *>())));

  // Destroy() deletes the container on success.
  EXPECT_EQ(Status::OK, lmctfy_->Destroy(mock_container));
}

TEST_F(ContainerApiImplTest, DestroyGetSubcontainersFails) {
  unique_ptr<MockContainer> mock_container(
      new StrictMock<MockContainer>("/test"));

  EXPECT_CALL(*mock_container, Destroy())
      .WillRepeatedly(Return(Status::OK));
  EXPECT_CALL(*mock_container, ListSubcontainers(Container::LIST_RECURSIVE))
      .WillRepeatedly(
          Return(StatusOr<vector<Container *>>(Status::CANCELLED)));

  // Destroy() deletes the container on success.
  EXPECT_EQ(Status::CANCELLED, lmctfy_->Destroy(mock_container.get()));
}

TEST_F(ContainerApiImplTest, DestroyContainerDestroyFails) {
  unique_ptr<MockContainer> mock_container(
      new StrictMock<MockContainer>("/test"));

  EXPECT_CALL(*mock_container, Destroy())
      .WillRepeatedly(Return(Status::CANCELLED));
  EXPECT_CALL(*mock_container, ListSubcontainers(Container::LIST_RECURSIVE))
      .WillRepeatedly(
          Return(StatusOr<vector<Container *>>(vector<Container *>())));

  EXPECT_EQ(Status::CANCELLED, lmctfy_->Destroy(mock_container.get()));
}

// Tests for Destroy() when there are subcontainers to kill.

class ContainerApiImplDestroyTest : public ::testing::Test {
 public:
  void SetUp() {
    // Add dummy Global Resource Handler
    mock_namespace_handler_factory_ = new StrictMockNamespaceHandlerFactory();
    vector<ResourceHandlerFactory *> resource_factories {
        new StrictMockResourceHandlerFactory(RESOURCE_CPU),
    };

    mock_tasks_handler_factory_ = new StrictMockTasksHandlerFactory();
    mock_cgroup_factory_ = new StrictMockCgroupFactory();
    active_notifications_ = new ActiveNotifications();
    mock_eventfd_notifications_ = MockEventFdNotifications::NewStrict();
    mock_lmctfy_.reset(new StrictMock<MockContainerApiImpl>(
        mock_tasks_handler_factory_,
        mock_cgroup_factory_,
        resource_factories,
        &mock_kernel_.Mock(), active_notifications_,
        mock_namespace_handler_factory_,
        mock_eventfd_notifications_));
    mock_container_ = new StrictMock<MockContainer>("/test");
  }

 protected:
  unique_ptr<MockContainerApiImpl> mock_lmctfy_;
  MockTasksHandlerFactory *mock_tasks_handler_factory_;
  MockCgroupFactory *mock_cgroup_factory_;
  MockContainer *mock_container_;
  ActiveNotifications *active_notifications_;
  MockEventFdNotifications *mock_eventfd_notifications_;
  MockNamespaceHandlerFactory *mock_namespace_handler_factory_;
  MockKernelApiOverride mock_kernel_;
};

TEST_F(ContainerApiImplDestroyTest, DestroyOnlyChildrenContainersSuccess) {
  MockContainer *mock_sub = new StrictMock<MockContainer>("/test/sub");
  vector<Container *> subcontainers = {mock_sub};

  // Return two children subcontainers with no children
  EXPECT_CALL(*mock_container_, ListSubcontainers(Container::LIST_RECURSIVE))
      .WillRepeatedly(Return(StatusOr<vector<Container *>>(subcontainers)));

  // Call to Destroy() for all containers, children destroyed first
  {
    InSequence s;
    EXPECT_CALL(*mock_sub, Destroy())
        .WillRepeatedly(Return(Status::OK));
    EXPECT_CALL(*mock_container_, Destroy())
        .WillRepeatedly(Return(Status::OK));
  }

  EXPECT_EQ(Status::OK,
            mock_lmctfy_->ContainerApiImpl::Destroy(mock_container_));
}

TEST_F(ContainerApiImplDestroyTest, DestroyOnlyMultipleChildrenContainersSuccess) {
  vector<ResourceHandler *> empty_handlers;
  MockContainer *mock_sub1 = new StrictMock<MockContainer>("/test/sub1");
  MockContainer *mock_sub2 = new StrictMock<MockContainer>("/test/sub2");
  vector<Container *> subcontainers = {mock_sub1, mock_sub2};

  // Return two children subcontainers with no children
  EXPECT_CALL(*mock_container_, ListSubcontainers(Container::LIST_RECURSIVE))
      .WillRepeatedly(Return(StatusOr<vector<Container *>>(subcontainers)));

  // Call to Destroy() for all containers
  EXPECT_CALL(*mock_container_, Destroy())
      .WillRepeatedly(Return(Status::OK));
  EXPECT_CALL(*mock_sub1, Destroy())
      .WillRepeatedly(Return(Status::OK));
  EXPECT_CALL(*mock_sub2, Destroy())
      .WillRepeatedly(Return(Status::OK));

  EXPECT_EQ(Status::OK,
            mock_lmctfy_->ContainerApiImpl::Destroy(mock_container_));
}

TEST_F(ContainerApiImplDestroyTest, DestroyOnlyChildrenContainersDestroyFails) {
  vector<ResourceHandler *> empty_handlers;
  MockContainer *mock_sub1 = new StrictMock<MockContainer>("/test/sub1");
  MockContainer *mock_sub2 = new StrictMock<MockContainer>("/test/sub2");
  vector<Container *> subcontainers = {mock_sub1, mock_sub2};

  // Return two children subcontainers with no children
  EXPECT_CALL(*mock_container_, ListSubcontainers(Container::LIST_RECURSIVE))
      .WillRepeatedly(Return(StatusOr<vector<Container *>>(subcontainers)));

  // Call to Destroy() for all containers
  EXPECT_CALL(*mock_container_, Destroy())
      .WillRepeatedly(Return(Status::OK));
  EXPECT_CALL(*mock_sub1, Destroy())
      .WillRepeatedly(Return(Status::OK));
  EXPECT_CALL(*mock_sub2, Destroy())
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED,
            mock_lmctfy_->ContainerApiImpl::Destroy(mock_container_));
  delete mock_container_;
}

TEST_F(ContainerApiImplDestroyTest, DestroyChildrenContainersWithChildrenSuccess) {
  vector<ResourceHandler *> empty_handlers;
  MockContainer *mock_sub1 = new StrictMock<MockContainer>("/test/sub1");
  MockContainer *mock_sub2 = new StrictMock<MockContainer>("/test/sub2");
  MockContainer *mock_sub1_1 = new StrictMock<MockContainer>("/test/sub1/sub1");
  MockContainer *mock_sub2_1 = new StrictMock<MockContainer>("/test/sub2/sub1");
  vector<Container *> subcontainers = {mock_sub1, mock_sub2, mock_sub1_1,
                                       mock_sub2_1};

  // Return two children subcontainers with no children
  EXPECT_CALL(*mock_container_, ListSubcontainers(Container::LIST_RECURSIVE))
      .WillRepeatedly(Return(StatusOr<vector<Container *>>(subcontainers)));

  // Call to Destroy() for all containers, children killed before parents
  EXPECT_CALL(*mock_container_, Destroy())
      .WillRepeatedly(Return(Status::OK));
  {
    InSequence s;
    EXPECT_CALL(*mock_sub1_1, Destroy())
        .WillRepeatedly(Return(Status::OK));
    EXPECT_CALL(*mock_sub1, Destroy())
        .WillRepeatedly(Return(Status::OK));
  }
  {
    InSequence s;
    EXPECT_CALL(*mock_sub2_1, Destroy())
        .WillRepeatedly(Return(Status::OK));
    EXPECT_CALL(*mock_sub2, Destroy())
        .WillRepeatedly(Return(Status::OK));
  }

  EXPECT_EQ(Status::OK,
            mock_lmctfy_->ContainerApiImpl::Destroy(mock_container_));
}

// Tests for ResolveContainer

TEST_F(ContainerApiImplTest, ResolveContainerName) {
  StatusOr<string> statusor;

  // Failed detection
  EXPECT_CALL(*mock_tasks_handler_factory_, Detect(0))
      .WillRepeatedly(Return(StatusOr<string>(Status::CANCELLED)));

  statusor = CallResolveContainerName(".");
  EXPECT_FALSE(statusor.ok());

  // Map current container to /top
  EXPECT_CALL(*mock_tasks_handler_factory_, Detect(0))
      .WillRepeatedly(Return(StatusOr<string>("/top")));

  // Root references work
  statusor = CallResolveContainerName("/");
  statusor = statusor;
  EXPECT_TRUE(statusor.ok());
  EXPECT_EQ("/", statusor.ValueOrDie());
  statusor = CallResolveContainerName("//");
  EXPECT_TRUE(statusor.ok());
  EXPECT_EQ("/", statusor.ValueOrDie());

  // Relative container names work (./name, name)
  statusor = CallResolveContainerName("test");
  EXPECT_TRUE(statusor.ok());
  EXPECT_EQ("/top/test", statusor.ValueOrDie());
  statusor = CallResolveContainerName("./test");
  EXPECT_TRUE(statusor.ok());
  EXPECT_EQ("/top/test", statusor.ValueOrDie());
  statusor = CallResolveContainerName("./test/test2");
  EXPECT_TRUE(statusor.ok());
  EXPECT_EQ("/top/test/test2", statusor.ValueOrDie());
  statusor = CallResolveContainerName("a");
  EXPECT_TRUE(statusor.ok());
  EXPECT_EQ("/top/a", statusor.ValueOrDie());

  // Self-references work (., ./)
  statusor = CallResolveContainerName(".");
  EXPECT_TRUE(statusor.ok());
  EXPECT_EQ("/top", statusor.ValueOrDie());
  statusor = CallResolveContainerName("./");
  EXPECT_TRUE(statusor.ok());
  EXPECT_EQ("/top", statusor.ValueOrDie());

  // Parent references work
  statusor = CallResolveContainerName("..");
  EXPECT_TRUE(statusor.ok());
  EXPECT_EQ("/", statusor.ValueOrDie());
  statusor = CallResolveContainerName("../test");
  EXPECT_TRUE(statusor.ok());
  EXPECT_EQ("/test", statusor.ValueOrDie());
  statusor = CallResolveContainerName("/top/../test");
  EXPECT_TRUE(statusor.ok());
  EXPECT_EQ("/test", statusor.ValueOrDie());
  statusor = CallResolveContainerName("/..");
  EXPECT_TRUE(statusor.ok());
  EXPECT_EQ("/", statusor.ValueOrDie());
  statusor = CallResolveContainerName("./../test");
  EXPECT_TRUE(statusor.ok());
  EXPECT_EQ("/test", statusor.ValueOrDie());
  statusor = CallResolveContainerName("/test..test");
  EXPECT_TRUE(statusor.ok());
  EXPECT_EQ("/test..test", statusor.ValueOrDie());
  statusor = CallResolveContainerName("/test..");
  EXPECT_TRUE(statusor.ok());
  EXPECT_EQ("/test..", statusor.ValueOrDie());
  statusor = CallResolveContainerName("/test../test");
  EXPECT_TRUE(statusor.ok());
  EXPECT_EQ("/test../test", statusor.ValueOrDie());

  // Remove duplicate slashes
  statusor = CallResolveContainerName("//test/test2//test3///");
  EXPECT_TRUE(statusor.ok());
  EXPECT_EQ("/test/test2/test3", statusor.ValueOrDie());

  // Removes ./'s
  statusor = CallResolveContainerName("/test/././././test1///");
  EXPECT_TRUE(statusor.ok());
  EXPECT_EQ("/test/test1", statusor.ValueOrDie());

  // Check invalid names
  EXPECT_EQ(::util::error::INVALID_ARGUMENT,
            CallResolveContainerName("").status().error_code());
  EXPECT_EQ(::util::error::INVALID_ARGUMENT,
            CallResolveContainerName("<").status().error_code());
  EXPECT_EQ(::util::error::INVALID_ARGUMENT,
            CallResolveContainerName("+").status().error_code());
  EXPECT_EQ(::util::error::INVALID_ARGUMENT,
            CallResolveContainerName("/.badname").status().error_code());
  EXPECT_EQ(::util::error::INVALID_ARGUMENT,
            CallResolveContainerName("/_badname").status().error_code());
  EXPECT_EQ(::util::error::INVALID_ARGUMENT,
            CallResolveContainerName("../.badname").status().error_code());
  EXPECT_EQ(::util::error::INVALID_ARGUMENT,
            CallResolveContainerName("_badname").status().error_code());

  // Valid characters
  statusor = CallResolveContainerName("/text");
  EXPECT_TRUE(statusor.ok());
  EXPECT_EQ("/text", statusor.ValueOrDie());
  statusor = CallResolveContainerName("/90");
  EXPECT_TRUE(statusor.ok());
  EXPECT_EQ("/90", statusor.ValueOrDie());
  statusor = CallResolveContainerName("/with.dots.in.name");
  EXPECT_TRUE(statusor.ok());
  EXPECT_EQ("/with.dots.in.name", statusor.ValueOrDie());
  statusor = CallResolveContainerName("/with-dash-in-name");
  EXPECT_TRUE(statusor.ok());
  EXPECT_EQ("/with-dash-in-name", statusor.ValueOrDie());
  statusor = CallResolveContainerName("/with_underscore_in_name");
  EXPECT_TRUE(statusor.ok());
  EXPECT_EQ("/with_underscore_in_name", statusor.ValueOrDie());
  statusor = CallResolveContainerName("/with/slash/in/name");
  EXPECT_TRUE(statusor.ok());
  EXPECT_EQ("/with/slash/in/name", statusor.ValueOrDie());
}

// Tests for Detect()

TEST_F(ContainerApiImplTest, DetectSuccess) {
  EXPECT_CALL(*mock_tasks_handler_factory_, Detect(1))
      .WillRepeatedly(Return(StatusOr<string>("/sys")));

  StatusOr<string> statusor = lmctfy_->Detect(1);
  ASSERT_TRUE(statusor.ok());
  EXPECT_EQ("/sys", statusor.ValueOrDie());
}

TEST_F(ContainerApiImplTest, DetectNoSuchContainer) {
  EXPECT_CALL(*mock_tasks_handler_factory_, Detect(1))
      .WillRepeatedly(Return(StatusOr<string>(Status::CANCELLED)));

  StatusOr<string> statusor = lmctfy_->Detect(1);
  ASSERT_FALSE(statusor.ok());
  EXPECT_EQ(Status::CANCELLED, statusor.status());
}

static bool CompareByName(Container *c1, Container *c2) {
  return c1->name() < c2->name();
}

static const char kContainerName[] = "/tasks/test";
static const char kParentContainer[] = "/tasks";
static const char kRootContainer[] = "/";

class ContainerImplTest : public ::testing::Test {
 public:
  ContainerImplTest()
      : list_policies_({Container::LIST_SELF, Container::LIST_RECURSIVE}),
        stats_types_({Container::STATS_FULL, Container::STATS_SUMMARY}) {}

  void SetUp() {
    InitContainer(kContainerName);
  }

  // Initialize the container used for testing. The container will have the
  // specified name.
  void InitContainer(const string &container_name) {
    mock_tasks_handler_ = new StrictMockTasksHandler(container_name);
    mock_resource_factory1_.reset(
        new StrictMockResourceHandlerFactory(RESOURCE_CPU));
    mock_resource_factory2_.reset(
        new StrictMockResourceHandlerFactory(RESOURCE_MEMORY));
    mock_resource_factory3_.reset(
        new StrictMockResourceHandlerFactory(RESOURCE_FILESYSTEM));
    mock_namespace_handler_factory_.reset(
        new StrictMockNamespaceHandlerFactory());

    mock_lmctfy_.reset(new StrictMock<MockContainerApiImpl>(
        new StrictMockTasksHandlerFactory(),
        new StrictMockCgroupFactory(),
        vector<ResourceHandlerFactory *>(), &mock_kernel_.Mock(),
        new ActiveNotifications(), new StrictMockNamespaceHandlerFactory(),
        MockEventFdNotifications::NewStrict()));

    ResourceFactoryMap factory_map = {
      {RESOURCE_CPU, mock_resource_factory1_.get()},
      {RESOURCE_MEMORY, mock_resource_factory2_.get()},
      {RESOURCE_FILESYSTEM, mock_resource_factory3_.get()},
    };

    mock_freezer_controller_ = new StrictMockFreezerController();

    container_.reset(new ContainerImpl(
        container_name,
        mock_tasks_handler_,
        factory_map,
        mock_lmctfy_.get(),
        &mock_kernel_.Mock(),
        mock_namespace_handler_factory_.get(),
        &active_notifications_,
        unique_ptr<FreezerController>(mock_freezer_controller_)));

    ExpectExists(true);
  }

  // Translate from Container::ListType to TasksHandler::ListPolicy.
  TasksHandler::ListType ToTasksHandlerListType(Container::ListPolicy policy) {
    return policy == Container::LIST_SELF ? TasksHandler::ListType::SELF
                                          : TasksHandler::ListType::RECURSIVE;
  }

  // Expect the created container to have the subcontainers with the specified
  // names. This creates mocks for those subcontainers and adds them to
  // container_map_.
  void ExpectSubcontainers(const vector<string> &subcontainer_names,
                           Container::ListPolicy policy) {
    vector<Container *> subcontainers;

    // Create the subcontainers and expect their Get()s.
    for (const string &name : subcontainer_names) {
      MockContainer *sub = new StrictMockContainer(name);

      EXPECT_CALL(*mock_lmctfy_, Get(name))
          .WillRepeatedly(Return(sub));

      subcontainers.push_back(sub);
      container_map_[name] = sub;
    }

    EXPECT_CALL(*mock_tasks_handler_,
                ListSubcontainers(ToTasksHandlerListType(policy)))
        .WillRepeatedly(Return(subcontainer_names));
  }

  // Expect the specified container vector to contain all the containers in the
  // container_map_ and for them to be sorted.
  void ExpectContainers(const vector<Container *> &actual) {
    EXPECT_EQ(container_map_.size(), actual.size());
    for (const auto &name_mock_pair : container_map_) {
      EXPECT_THAT(actual, Contains(name_mock_pair.second));
    }

    vector<Container *> sorted_actual = actual;
    sort(sorted_actual.begin(), sorted_actual.end(), &CompareByName);
    EXPECT_THAT(actual, ContainerEq(sorted_actual));
  }

  // Expects that 2 subcontainers exist under the current container and that
  // ListProcesses/ListThreads is called on them (depending on the value of
  // list_processes). Subcontainer1 returns pids1 and subcontainer2 returns
  // pids2.
  // Returns: a vector with the two mock subcontainer objects.
  // These objects are owned by the caller.
  vector<MockContainer *> ExpectSubcontainersListPids(
      bool list_processes,
      const vector<pid_t> &pids1,
      const vector<pid_t> &pids2) {
    // Expect 2 subcontainers each with PIDs.
    const string kSub1 = JoinPath(container_->name(), "sub1");
    const string kSub2 = JoinPath(container_->name(), "sub2");
    ExpectSubcontainers({kSub1, kSub2}, Container::LIST_SELF);

    if (list_processes) {
      EXPECT_CALL(*container_map_[kSub1], ListProcesses(Container::LIST_SELF))
          .WillRepeatedly(Return(pids1));
      EXPECT_CALL(*container_map_[kSub2], ListProcesses(Container::LIST_SELF))
          .WillRepeatedly(Return(pids2));
    } else {
      EXPECT_CALL(*container_map_[kSub1], ListThreads(Container::LIST_SELF))
          .WillRepeatedly(Return(pids1));
      EXPECT_CALL(*container_map_[kSub2], ListThreads(Container::LIST_SELF))
          .WillRepeatedly(Return(pids2));
    }

    return {container_map_[kSub1], container_map_[kSub2]};
  }

  // Expect a call to GetResourceHandlers() where the status of the last
  // resource factory returns last_status. Returns a vector of the created
  // MockResourceHandlers which are owned by the caller. Will always be
  // non-empty.
  //
  // Returns 3 ResourceHandlers:
  // - CPU: Attached to kContainerName
  // - Memory: Attached to kContainerName.
  // - Global: Attached to kParentContainer.
  vector<MockResourceHandler *> ExpectGetResourceHandlers(Status last_status) {
    vector<MockResourceHandler *> output;

    MockResourceHandler *mock_handler1 = new StrictMockResourceHandler(
        kContainerName, RESOURCE_CPU);
    EXPECT_CALL(*mock_resource_factory1_, Get(kContainerName))
        .WillRepeatedly(Return(mock_handler1));
    output.push_back(mock_handler1);

    // Only return the handler if the status was OK.
    if (last_status.ok()) {
      MockResourceHandler *mock_handler2 = new StrictMockResourceHandler(
          kContainerName, RESOURCE_MEMORY);
      EXPECT_CALL(*mock_resource_factory2_, Get(kContainerName))
          .WillRepeatedly(Return(mock_handler2));
      output.push_back(mock_handler2);
    } else {
      EXPECT_CALL(*mock_resource_factory2_, Get(kContainerName))
          .WillRepeatedly(Return(last_status));
    }

    MockResourceHandler *mock_handler3 = new StrictMockResourceHandler(
        kParentContainer, RESOURCE_FILESYSTEM);
    EXPECT_CALL(*mock_resource_factory3_, Get(kContainerName))
        .WillRepeatedly(Return(Status(::util::error::NOT_FOUND, "")));
    EXPECT_CALL(*mock_resource_factory3_, Get(kParentContainer))
        .WillRepeatedly(Return(mock_handler3));
    output.push_back(mock_handler3);

    return output;
  }

  MockNamespaceHandler *ExpectGetNamespaceHandler() {
    MockNamespaceHandler *result =
        new StrictMockNamespaceHandler(kContainerName, RESOURCE_VIRTUALHOST);
    EXPECT_CALL(*mock_namespace_handler_factory_,
                GetNamespaceHandler(kContainerName))
        .WillOnce(Return(result));
    return result;
  }

  // Expect Kill() to be called on the specified PIDs and returning ret_val.
  // Expect this to happen num_tries times.
  void ExpectKill(const vector<pid_t> &pids, int ret_val,
                  Cardinality num_tries) {
    for (pid_t pid : pids) {
      EXPECT_CALL(mock_kernel_.Mock(), Kill(pid))
          .Times(num_tries)
          .WillRepeatedly(Return(ret_val));
    }
  }

  // Expect a call to KillAll(). Fills one in as if the container was already
  // empty.
  void ExpectSimpleKillAll(Status status) {
    // No processes or threads.
    if (status.ok()) {
      EXPECT_CALL(*mock_tasks_handler_,
                  ListProcesses(TasksHandler::ListType::SELF))
          .WillRepeatedly(Return(vector<pid_t>()));
      EXPECT_CALL(*mock_tasks_handler_,
                  ListThreads(TasksHandler::ListType::SELF))
          .WillRepeatedly(Return(vector<pid_t>()));
    } else {
      EXPECT_CALL(*mock_tasks_handler_,
                  ListProcesses(TasksHandler::ListType::SELF))
          .WillRepeatedly(Return(status));
      EXPECT_CALL(*mock_tasks_handler_,
                  ListThreads(TasksHandler::ListType::SELF))
          .WillRepeatedly(Return(status));
    }

    EXPECT_CALL(mock_kernel_.Mock(), Usleep(
        FLAGS_lmctfy_ms_delay_between_kills * 1000))
        .WillRepeatedly(Return(0));
  }

  void ExpectEnterInto(pid_t pid, Status status) {
    vector<pid_t> tids = {pid};

    EXPECT_CALL(*mock_tasks_handler_, TrackTasks(tids))
        .WillRepeatedly(Return(Status::OK));

    EXPECT_CALL(*mock_freezer_controller_, Enter(pid))
        .WillOnce(Return(Status::OK));

    // Enter() takes ownership of the return of GetResourceHandlers().
    vector<MockResourceHandler *> handlers = ExpectGetResourceHandlers(
        Status::OK);
    for (MockResourceHandler *handler : handlers) {
      EXPECT_CALL(*handler, Enter(tids))
          .WillRepeatedly(Return(status));
    }
  }

  // Expect Enter() to be called on the specified TID and return with the
  // specified status.
  void ExpectEnter(pid_t pid, Status status) {
    // Enter() takes ownership of the return of GetResourceHandlers().
    MockNamespaceHandler *namespace_handler = ExpectGetNamespaceHandler();
    EXPECT_CALL(*namespace_handler, IsDifferentVirtualHost(vector<pid_t>{pid}))
        .WillRepeatedly(Return(false));

    ExpectEnterInto(pid, status);
  }

  // Expect a call to Exists() that returns true iff exists is specified.
  void ExpectExists(bool exists) {
    EXPECT_CALL(*mock_lmctfy_, Exists(kContainerName))
        .WillRepeatedly(Return(exists));
  }

  // Wrappers for testing private functions.

  StatusOr<vector<ResourceHandler *>> CallGetResourceHandlers() {
    return container_->GetResourceHandlers();
  }

  Status CallKillTasks(ContainerImpl::ListType type) {
    return container_->KillTasks(type);
  }

 protected:
  // Map of created mock containers (from their names to the mock object).
  map<string, MockContainer *> container_map_;

  const vector<Container::ListPolicy> list_policies_;
  const vector<Container::StatsType> stats_types_;

  MockTasksHandler *mock_tasks_handler_;
  unique_ptr<MockResourceHandlerFactory> mock_resource_factory1_;
  unique_ptr<MockResourceHandlerFactory> mock_resource_factory2_;
  unique_ptr<MockResourceHandlerFactory> mock_resource_factory3_;

  ActiveNotifications active_notifications_;
  unique_ptr<ContainerImpl> container_;
  unique_ptr<MockContainerApiImpl> mock_lmctfy_;
  MockFreezerController *mock_freezer_controller_;
  unique_ptr<MockNamespaceHandlerFactory> mock_namespace_handler_factory_;
  MockKernelApiOverride mock_kernel_;
};

// Tests for ListSubcontainers().
typedef ContainerImplTest ListSubcontainersTest;

TEST_F(ListSubcontainersTest, NoContainer) {
  // Call for self and recursive
  for (const auto &policy : list_policies_) {
    ExpectExists(false);

    EXPECT_ERROR_CODE(NOT_FOUND, container_->ListSubcontainers(policy));
  }
}

TEST_F(ListSubcontainersTest, NoSubcontainers) {
  // Call for self and recursive
  for (const auto &policy : list_policies_) {
    ExpectSubcontainers({}, policy);

    StatusOr<vector<Container *>> statusor = container_->ListSubcontainers(
        policy);
    ASSERT_OK(statusor);
    EXPECT_EQ(0, statusor.ValueOrDie().size());
  }
}

TEST_F(ListSubcontainersTest, OneLevel) {
  // Call for self and recursive
  for (const auto &policy : list_policies_) {
    ExpectSubcontainers(
        {JoinPath(kContainerName, "sub2"), JoinPath(kContainerName, "sub1")},
        policy);

    StatusOr<vector<Container *>> statusor = container_->ListSubcontainers(
        policy);
    ASSERT_OK(statusor);
    vector<Container *> subcontainers = statusor.ValueOrDie();
    EXPECT_EQ(2, subcontainers.size());
    ExpectContainers(subcontainers);
    STLDeleteElements(&subcontainers);
  }
}

TEST_F(ListSubcontainersTest, ListFails) {
  // Call for self and recursive
  for (const auto &policy : list_policies_) {
    EXPECT_CALL(*mock_tasks_handler_,
                ListSubcontainers(policy == Container::LIST_SELF
                                      ? TasksHandler::ListType::SELF
                                      : TasksHandler::ListType::RECURSIVE))
        .WillRepeatedly(Return(StatusOr<vector<string>>(Status::CANCELLED)));

    EXPECT_EQ(Status::CANCELLED,
              container_->ListSubcontainers(policy).status());
  }
}

TEST_F(ListSubcontainersTest, ContainerApiGetFails) {
  const string kSub = JoinPath(kContainerName, "sub1");

  // Call for self and recursive
  for (const auto &policy : list_policies_) {
    ExpectSubcontainers({kSub}, policy);

    // Getting one of the containers fails.
    EXPECT_CALL(*mock_lmctfy_, Get(kSub))
        .WillRepeatedly(Return(StatusOr<Container *>(Status::CANCELLED)));

    EXPECT_EQ(Status::CANCELLED,
              container_->ListSubcontainers(policy).status());

    // Since we failed the Get() it is safe to delete the mock container.
    STLDeleteValues(&container_map_);
  }
}

// Tests for ListThreads().
typedef ContainerImplTest ListThreadsTest;

TEST_F(ListThreadsTest, NoContainer) {
  // Call for self and recursive
  for (const auto &policy : list_policies_) {
    ExpectExists(false);

    EXPECT_ERROR_CODE(NOT_FOUND, container_->ListThreads(policy));
  }
}

TEST_F(ListThreadsTest, Success) {
  const vector<pid_t> kPids = {1, 2, 3, 4};

  // Call for self and recursive
  for (const auto &policy : list_policies_) {
    ExpectExists(true);

    EXPECT_CALL(*mock_tasks_handler_,
                ListThreads(ToTasksHandlerListType(policy)))
        .WillRepeatedly(Return(kPids));

    StatusOr<vector<pid_t>> statusor = container_->ListThreads(policy);
    ASSERT_OK(statusor);
    EXPECT_EQ(kPids, statusor.ValueOrDie());
  }
}

TEST_F(ListThreadsTest, ListThreadsFails) {
  // Call for self and recursive
  for (const auto &policy : list_policies_) {
    ExpectExists(true);

    EXPECT_CALL(*mock_tasks_handler_,
                ListThreads(ToTasksHandlerListType(policy)))
        .WillRepeatedly(Return(Status::CANCELLED));

    EXPECT_EQ(Status::CANCELLED, container_->ListThreads(policy).status());
  }
}

// Tests for ListProcesses().
typedef ContainerImplTest ListProcessesTest;

TEST_F(ListProcessesTest, NoContainer) {
  // Call for self and recursive
  for (const auto &policy : list_policies_) {
    ExpectExists(false);

    EXPECT_ERROR_CODE(NOT_FOUND, container_->ListProcesses(policy));
  }
}

TEST_F(ListProcessesTest, Success) {
  const vector<pid_t> kPids = {1, 2, 3, 4};

  // Call for self and recursive
  for (const auto &policy : list_policies_) {
    ExpectExists(true);

    EXPECT_CALL(*mock_tasks_handler_,
                ListProcesses(ToTasksHandlerListType(policy)))
        .WillRepeatedly(Return(kPids));

    StatusOr<vector<pid_t>> statusor = container_->ListProcesses(policy);
    ASSERT_OK(statusor);
    EXPECT_EQ(kPids, statusor.ValueOrDie());
  }
}

TEST_F(ListProcessesTest, ListProcessesFails) {
  // Call for self and recursive
  for (const auto &policy : list_policies_) {
    ExpectExists(true);

    EXPECT_CALL(*mock_tasks_handler_,
                ListProcesses(ToTasksHandlerListType(policy)))
        .WillRepeatedly(Return(Status::CANCELLED));

    EXPECT_EQ(Status::CANCELLED, container_->ListProcesses(policy).status());
  }
}

// Tests for GetResourceHandlers().

TEST_F(ContainerImplTest, GetResourceHandlersSuccess) {
  EXPECT_CALL(*mock_resource_factory1_, Get(kContainerName))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          new StrictMockResourceHandler(kContainerName, RESOURCE_CPU))));
  EXPECT_CALL(*mock_resource_factory2_, Get(kContainerName))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          new StrictMockResourceHandler(kContainerName, RESOURCE_MEMORY))));
  EXPECT_CALL(*mock_resource_factory3_, Get(kContainerName))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          new StrictMockResourceHandler(kContainerName, RESOURCE_FILESYSTEM))));
  EXPECT_CALL(*mock_namespace_handler_factory_,
              GetNamespaceHandler(kContainerName))
      .WillRepeatedly(Return(StatusOr<NamespaceHandler *>(
          new StrictMockNamespaceHandler(kContainerName,
                                         RESOURCE_VIRTUALHOST))));

  StatusOr<vector<ResourceHandler *>> statusor = CallGetResourceHandlers();
  ASSERT_TRUE(statusor.ok());

  vector<ResourceHandler *> result = statusor.ValueOrDie();
  EXPECT_EQ(3, result.size());
  STLDeleteElements(&result);
}

TEST_F(ContainerImplTest, GetResourceHandlersAttachToParent) {
  // Set memory not to find the current container.
  EXPECT_CALL(*mock_resource_factory1_, Get(kContainerName))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          new StrictMockResourceHandler(kContainerName, RESOURCE_CPU))));
  EXPECT_CALL(*mock_resource_factory2_, Get(kContainerName))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          Status(::util::error::NOT_FOUND, ""))));
  EXPECT_CALL(*mock_resource_factory2_, Get(kParentContainer))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          new StrictMockResourceHandler(kContainerName, RESOURCE_MEMORY))));
  EXPECT_CALL(*mock_resource_factory3_, Get(kContainerName))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          new StrictMockResourceHandler(kContainerName, RESOURCE_FILESYSTEM))));
  EXPECT_CALL(*mock_namespace_handler_factory_,
              GetNamespaceHandler(kContainerName))
      .WillRepeatedly(Return(StatusOr<NamespaceHandler *>(
          new StrictMockNamespaceHandler(kContainerName,
                                         RESOURCE_VIRTUALHOST))));

  StatusOr<vector<ResourceHandler *>> statusor = CallGetResourceHandlers();
  ASSERT_TRUE(statusor.ok());

  vector<ResourceHandler *> result = statusor.ValueOrDie();
  EXPECT_EQ(3, result.size());
  STLDeleteElements(&result);
}

TEST_F(ContainerImplTest, GetResourceHandlersAttachToRoot) {
  // Set memory not to find current or parent container.
  EXPECT_CALL(*mock_resource_factory1_, Get(kContainerName))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          new StrictMockResourceHandler(kContainerName, RESOURCE_CPU))));
  EXPECT_CALL(*mock_resource_factory2_, Get(kContainerName))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          Status(::util::error::NOT_FOUND, ""))));
  EXPECT_CALL(*mock_resource_factory2_, Get(kParentContainer))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          Status(::util::error::NOT_FOUND, ""))));
  EXPECT_CALL(*mock_resource_factory2_, Get(kRootContainer))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          new StrictMockResourceHandler(kContainerName, RESOURCE_MEMORY))));
  EXPECT_CALL(*mock_resource_factory3_, Get(kContainerName))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          new StrictMockResourceHandler(kContainerName, RESOURCE_FILESYSTEM))));
  EXPECT_CALL(*mock_namespace_handler_factory_,
              GetNamespaceHandler(kContainerName))
      .WillRepeatedly(Return(StatusOr<NamespaceHandler *>(
          new StrictMockNamespaceHandler(kContainerName,
                                         RESOURCE_VIRTUALHOST))));

  StatusOr<vector<ResourceHandler *>> statusor = CallGetResourceHandlers();
  ASSERT_TRUE(statusor.ok()) << statusor.status();

  vector<ResourceHandler *> result = statusor.ValueOrDie();
  EXPECT_EQ(3, result.size());
  STLDeleteElements(&result);
}

TEST_F(ContainerImplTest, GetResourceHandlersErrorAttaching) {
  EXPECT_CALL(*mock_resource_factory1_, Get(kContainerName))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          new StrictMockResourceHandler(kContainerName, RESOURCE_CPU))));
  EXPECT_CALL(*mock_resource_factory2_, Get(kContainerName))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(Status::CANCELLED)));
  EXPECT_CALL(*mock_resource_factory3_, Get(kContainerName))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          new StrictMockResourceHandler(kContainerName, RESOURCE_FILESYSTEM))));
  EXPECT_CALL(*mock_namespace_handler_factory_,
              GetNamespaceHandler(kContainerName))
      .WillRepeatedly(Return(StatusOr<NamespaceHandler *>(
          new StrictMockNamespaceHandler(kContainerName,
                                         RESOURCE_VIRTUALHOST))));

  ASSERT_FALSE(CallGetResourceHandlers().ok());
}

TEST_F(ContainerImplTest, GetResourceHandlersErrorAttachingToParent) {
  EXPECT_CALL(*mock_resource_factory1_, Get(kContainerName))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          new StrictMockResourceHandler(kContainerName, RESOURCE_CPU))));
  EXPECT_CALL(*mock_resource_factory2_, Get(kContainerName))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          Status(::util::error::NOT_FOUND, ""))));
  EXPECT_CALL(*mock_resource_factory2_, Get(kParentContainer))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(Status::CANCELLED)));
  EXPECT_CALL(*mock_resource_factory3_, Get(kContainerName))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          new StrictMockResourceHandler(kContainerName, RESOURCE_FILESYSTEM))));
  EXPECT_CALL(*mock_namespace_handler_factory_,
              GetNamespaceHandler(kContainerName))
      .WillRepeatedly(Return(StatusOr<NamespaceHandler *>(
          new StrictMockNamespaceHandler(kContainerName,
                                         RESOURCE_VIRTUALHOST))));

  ASSERT_FALSE(CallGetResourceHandlers().ok());
}

TEST_F(ContainerImplTest, GetResourceHandlersErrorAttachingToRoot) {
  EXPECT_CALL(*mock_resource_factory1_, Get(kContainerName))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          new StrictMockResourceHandler(kContainerName, RESOURCE_CPU))));
  EXPECT_CALL(*mock_resource_factory2_, Get(kContainerName))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          Status(::util::error::NOT_FOUND, ""))));
  EXPECT_CALL(*mock_resource_factory2_, Get(kParentContainer))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          Status(::util::error::NOT_FOUND, ""))));
  EXPECT_CALL(*mock_resource_factory2_, Get(kRootContainer))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(Status::CANCELLED)));
  EXPECT_CALL(*mock_resource_factory3_, Get(kContainerName))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          new StrictMockResourceHandler(kContainerName, RESOURCE_FILESYSTEM))));
  EXPECT_CALL(*mock_namespace_handler_factory_,
              GetNamespaceHandler(kContainerName))
      .WillRepeatedly(Return(StatusOr<NamespaceHandler *>(
          new StrictMockNamespaceHandler(kContainerName,
                                         RESOURCE_VIRTUALHOST))));

  ASSERT_FALSE(CallGetResourceHandlers().ok());
}

TEST_F(ContainerImplTest, GetResourceHandlersRootNotFound) {
  EXPECT_CALL(*mock_resource_factory1_, Get(kContainerName))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          new StrictMockResourceHandler(kContainerName, RESOURCE_CPU))));
  EXPECT_CALL(*mock_resource_factory2_, Get(kContainerName))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          Status(::util::error::NOT_FOUND, ""))));
  EXPECT_CALL(*mock_resource_factory2_, Get(kParentContainer))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          Status(::util::error::NOT_FOUND, ""))));
  EXPECT_CALL(*mock_resource_factory2_, Get(kRootContainer))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          Status(::util::error::NOT_FOUND, ""))));
  EXPECT_CALL(*mock_resource_factory3_, Get(kContainerName))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          new StrictMockResourceHandler(kContainerName, RESOURCE_FILESYSTEM))));
  EXPECT_CALL(*mock_namespace_handler_factory_,
              GetNamespaceHandler(kContainerName))
      .WillRepeatedly(Return(StatusOr<NamespaceHandler *>(
          new StrictMockNamespaceHandler(kContainerName,
                                         RESOURCE_VIRTUALHOST))));

  ASSERT_FALSE(CallGetResourceHandlers().ok());
}

TEST_F(ContainerImplTest, GetResourceHandlersOnRootContainerSuccess) {
  InitContainer(kRootContainer);

  EXPECT_CALL(*mock_resource_factory1_, Get(kRootContainer))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          new StrictMockResourceHandler(kRootContainer, RESOURCE_CPU))));
  EXPECT_CALL(*mock_resource_factory2_, Get(kRootContainer))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          new StrictMockResourceHandler(kRootContainer, RESOURCE_MEMORY))));
  EXPECT_CALL(*mock_resource_factory3_, Get(kRootContainer))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          new StrictMockResourceHandler(kRootContainer, RESOURCE_FILESYSTEM))));
  EXPECT_CALL(*mock_namespace_handler_factory_,
              GetNamespaceHandler(kRootContainer))
      .WillRepeatedly(Return(StatusOr<NamespaceHandler *>(
          new StrictMockNamespaceHandler(kRootContainer,
                                         RESOURCE_VIRTUALHOST))));

  StatusOr<vector<ResourceHandler *>> statusor = CallGetResourceHandlers();
  ASSERT_TRUE(statusor.ok());

  vector<ResourceHandler *> result = statusor.ValueOrDie();
  EXPECT_EQ(3, result.size());
  STLDeleteElements(&result);
}

TEST_F(ContainerImplTest, GetResourceHandlersOnRootContainerNotFound) {
  InitContainer(kRootContainer);

  EXPECT_CALL(*mock_resource_factory1_, Get(kRootContainer))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          new StrictMockResourceHandler(kRootContainer, RESOURCE_CPU))));
  EXPECT_CALL(*mock_resource_factory2_, Get(kRootContainer))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          Status(::util::error::NOT_FOUND, ""))));
  EXPECT_CALL(*mock_resource_factory3_, Get(kRootContainer))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          new StrictMockResourceHandler(kRootContainer, RESOURCE_FILESYSTEM))));

  ASSERT_FALSE(CallGetResourceHandlers().ok());
}

TEST_F(ContainerImplTest, GetResourceHandlersOnRootContainerError) {
  InitContainer(kRootContainer);

  EXPECT_CALL(*mock_resource_factory1_, Get(kRootContainer))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          new StrictMockResourceHandler(kRootContainer, RESOURCE_CPU))));
  EXPECT_CALL(*mock_resource_factory2_, Get(kRootContainer))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(Status::CANCELLED)));
  EXPECT_CALL(*mock_resource_factory3_, Get(kRootContainer))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          new StrictMockResourceHandler(kRootContainer, RESOURCE_FILESYSTEM))));

  ASSERT_FALSE(CallGetResourceHandlers().ok());
}

// Tests for Stats().

TEST_F(ContainerImplTest, StatsNoContainer) {
  for (Container::StatsType type : stats_types_) {
    ExpectExists(false);

    EXPECT_ERROR_CODE(NOT_FOUND, container_->Stats(type));
  }
}

// Append the specified type to the output. Does not actually fill with values.
void AppendCpuStats(Container::StatsType type, ContainerStats *output) {
  output->mutable_cpu();
}
void AppendMemoryStats(Container::StatsType type, ContainerStats *output) {
  output->mutable_memory();
}

TEST_F(ContainerImplTest, StatsSuccess) {
  for (Container::StatsType type : stats_types_) {
    vector<MockResourceHandler *> mock_handlers = ExpectGetResourceHandlers(
        Status::OK);
    MockNamespaceHandler *mock_namespace_handler = ExpectGetNamespaceHandler();

    // Each resource handler has 2 stat pairs (except Global which is not
    // attached to this container).
    for (MockResourceHandler *mock_handler : mock_handlers) {
      if (mock_handler->type() == RESOURCE_CPU) {
        EXPECT_CALL(*mock_handler, Stats(type, NotNull()))
            .WillRepeatedly(DoAll(Invoke(&AppendCpuStats), Return(Status::OK)));
      } else if (mock_handler->type() == RESOURCE_MEMORY) {
        EXPECT_CALL(*mock_handler, Stats(type, NotNull())).WillRepeatedly(
            DoAll(Invoke(&AppendMemoryStats), Return(Status::OK)));
      }
    }
    EXPECT_CALL(*mock_namespace_handler, Stats(type, NotNull()))
        .WillRepeatedly(Return(Status::OK));

    StatusOr<ContainerStats> statusor = container_->Stats(type);
    ASSERT_TRUE(statusor.ok());
    ContainerStats stats = statusor.ValueOrDie();
    EXPECT_TRUE(stats.has_cpu());
    EXPECT_TRUE(stats.has_memory());
    EXPECT_FALSE(stats.has_blockio());
    EXPECT_FALSE(stats.has_network());
    EXPECT_FALSE(stats.has_monitoring());
    EXPECT_FALSE(stats.has_filesystem());
  }
}

TEST_F(ContainerImplTest, StatsEmpty) {
  for (Container::StatsType type : stats_types_) {
    vector<MockResourceHandler *> mock_handlers = ExpectGetResourceHandlers(
        Status::OK);
    MockNamespaceHandler *mock_namespace_handler = ExpectGetNamespaceHandler();

    for (MockResourceHandler *mock_handler : mock_handlers) {
      EXPECT_CALL(*mock_handler, Stats(type, NotNull()))
          .WillRepeatedly(Return(Status::OK));
    }
    EXPECT_CALL(*mock_namespace_handler, Stats(type, NotNull()))
        .WillRepeatedly(Return(Status::OK));

    StatusOr<ContainerStats> statusor = container_->Stats(type);
    ASSERT_TRUE(statusor.ok());
    ContainerStats stats = statusor.ValueOrDie();
    EXPECT_FALSE(stats.has_cpu());
    EXPECT_FALSE(stats.has_memory());
    EXPECT_FALSE(stats.has_blockio());
    EXPECT_FALSE(stats.has_network());
    EXPECT_FALSE(stats.has_monitoring());
    EXPECT_FALSE(stats.has_filesystem());
  }
}

TEST_F(ContainerImplTest, StatsGetResourceHandlersFails) {
  for (Container::StatsType type : stats_types_) {
    vector<MockResourceHandler *> mock_handlers = ExpectGetResourceHandlers(
        Status::CANCELLED);
    ExpectGetNamespaceHandler();

    EXPECT_EQ(Status::CANCELLED, container_->Stats(type).status());
  }
}

TEST_F(ContainerImplTest, StatsResourceStatsFails) {
  for (Container::StatsType type : stats_types_) {
    vector<MockResourceHandler *> mock_handlers = ExpectGetResourceHandlers(
        Status::OK);
    ExpectGetNamespaceHandler();

    for (MockResourceHandler *mock_handler : mock_handlers) {
      EXPECT_CALL(*mock_handler, Stats(type, NotNull()))
          .WillRepeatedly(Return(Status::CANCELLED));
    }

    EXPECT_EQ(Status::CANCELLED, container_->Stats(type).status());
  }
}

// Tests for KillAll().

TEST_F(ContainerImplTest, KillAllNoContainer) {
  ExpectExists(false);

  EXPECT_ERROR_CODE(NOT_FOUND, container_->KillAll());
}

TEST_F(ContainerImplTest, KillAllAlreadyEmpty) {
  // No processes or threads.
  EXPECT_CALL(*mock_tasks_handler_, ListProcesses(TasksHandler::ListType::SELF))
      .WillRepeatedly(Return(vector<pid_t>()));
  EXPECT_CALL(*mock_tasks_handler_, ListThreads(TasksHandler::ListType::SELF))
      .WillRepeatedly(Return(vector<pid_t>()));

  EXPECT_CALL(mock_kernel_.Mock(), Usleep(
      FLAGS_lmctfy_ms_delay_between_kills * 1000))
      .WillRepeatedly(Return(0));

  EXPECT_TRUE(container_->KillAll().ok());
}

TEST_F(ContainerImplTest, KillAllDieOnFirstKill) {
  const vector<pid_t> kPids = {1, 2, 3};

  // Processes are killed on the first SIGKILL.
  EXPECT_CALL(*mock_tasks_handler_, ListProcesses(TasksHandler::ListType::SELF))
      .WillOnce(Return(kPids))
      .WillRepeatedly(Return(vector<pid_t>()));
  ExpectKill(kPids, 0, Exactly(1));

  // No threads.
  EXPECT_CALL(*mock_tasks_handler_, ListThreads(TasksHandler::ListType::SELF))
      .WillRepeatedly(Return(vector<pid_t>()));

  EXPECT_CALL(mock_kernel_.Mock(), Usleep(
      FLAGS_lmctfy_ms_delay_between_kills * 1000))
      .WillRepeatedly(Return(0));

  EXPECT_TRUE(container_->KillAll().ok());
}

TEST_F(ContainerImplTest, KillAllSigkillFails) {
  const vector<pid_t> kPids = {1, 2, 3};

  // Processes are killed on the first SIGTERM.
  EXPECT_CALL(*mock_tasks_handler_, ListProcesses(TasksHandler::ListType::SELF))
      .WillOnce(Return(kPids))
      .WillRepeatedly(Return(vector<pid_t>()));
  ExpectKill(kPids, -1, Exactly(1));

  // No threads.
  EXPECT_CALL(*mock_tasks_handler_, ListThreads(TasksHandler::ListType::SELF))
      .WillRepeatedly(Return(vector<pid_t>()));

  EXPECT_CALL(mock_kernel_.Mock(), Usleep(
      FLAGS_lmctfy_ms_delay_between_kills * 1000))
      .WillRepeatedly(Return(0));

  // We ignore KillTasks failing.
  EXPECT_TRUE(container_->KillAll().ok());
}

TEST_F(ContainerImplTest, KillAllDoNotDieOnFirstKill) {
  const vector<pid_t> kPids = {1, 2, 3};

  // Processes are killed after the second SIGKILL.
  EXPECT_CALL(*mock_tasks_handler_, ListProcesses(TasksHandler::ListType::SELF))
      .WillOnce(Return(kPids))
      .WillOnce(Return(kPids))
      .WillRepeatedly(Return(vector<pid_t>()));
  ExpectKill(kPids, 0, Exactly(2));

  // No threads.
  EXPECT_CALL(*mock_tasks_handler_, ListThreads(TasksHandler::ListType::SELF))
      .WillRepeatedly(Return(vector<pid_t>()));

  EXPECT_CALL(mock_kernel_.Mock(), Usleep(
      FLAGS_lmctfy_ms_delay_between_kills * 1000))
      .WillRepeatedly(Return(0));

  EXPECT_TRUE(container_->KillAll().ok());
}

TEST_F(ContainerImplTest, KillAllUsleepFails) {
  const vector<pid_t> kPids = {1, 2, 3};

  // Processes are killed after the second SIGKILL.
  EXPECT_CALL(*mock_tasks_handler_, ListProcesses(TasksHandler::ListType::SELF))
      .WillOnce(Return(kPids))
      .WillOnce(Return(kPids))
      .WillRepeatedly(Return(vector<pid_t>()));
  ExpectKill(kPids, 0, Exactly(2));

  // No threads.
  EXPECT_CALL(*mock_tasks_handler_, ListThreads(TasksHandler::ListType::SELF))
      .WillRepeatedly(Return(vector<pid_t>()));

  EXPECT_CALL(mock_kernel_.Mock(), Usleep(
      FLAGS_lmctfy_ms_delay_between_kills * 1000))
      .WillRepeatedly(Return(-1));

  // We ignore Usleep failing.
  EXPECT_TRUE(container_->KillAll().ok());
}

TEST_F(ContainerImplTest, KillAllUnkillableProcesses) {
  const vector<pid_t> kPids = {1, 2, 3};

  // Processes are never killed.
  EXPECT_CALL(*mock_tasks_handler_, ListProcesses(TasksHandler::ListType::SELF))
      .WillRepeatedly(Return(kPids));
  ExpectKill(kPids, 0, AtLeast(1));

  // No threads.
  EXPECT_CALL(*mock_tasks_handler_, ListThreads(TasksHandler::ListType::SELF))
      .WillRepeatedly(Return(vector<pid_t>()));

  EXPECT_CALL(mock_kernel_.Mock(), Usleep(
      FLAGS_lmctfy_ms_delay_between_kills * 1000))
      .WillRepeatedly(Return(0));

  Status status = container_->KillAll();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(::util::error::FAILED_PRECONDITION, status.error_code());
}

TEST_F(ContainerImplTest, KillAllWithTouristThreads) {
  const vector<pid_t> kPids = {1, 2, 3};
  const vector<pid_t> kTids = {4, 5, 6};

  // Processes are killed after the first SIGKILL.
  EXPECT_CALL(*mock_tasks_handler_, ListProcesses(TasksHandler::ListType::SELF))
      .WillOnce(Return(kPids))
      .WillRepeatedly(Return(vector<pid_t>()));
  ExpectKill(kPids, 0, Exactly(1));

  // Threads are killed on the first SIGKILL.
  EXPECT_CALL(*mock_tasks_handler_, ListThreads(TasksHandler::ListType::SELF))
      .WillOnce(Return(kTids))
      .WillRepeatedly(Return(vector<pid_t>()));
  ExpectKill(kTids, 0, Exactly(1));

  EXPECT_CALL(mock_kernel_.Mock(), Usleep(
      FLAGS_lmctfy_ms_delay_between_kills * 1000))
      .WillRepeatedly(Return(0));

  EXPECT_TRUE(container_->KillAll().ok());
}

TEST_F(ContainerImplTest, KillAllWithTouristThreadsSigkillFails) {
  const vector<pid_t> kPids = {1, 2, 3};
  const vector<pid_t> kTids = {4, 5, 6};

  // Processes are killed after the first SIGKILL.
  EXPECT_CALL(*mock_tasks_handler_, ListProcesses(TasksHandler::ListType::SELF))
      .WillOnce(Return(kPids))
      .WillRepeatedly(Return(vector<pid_t>()));
  ExpectKill(kPids, 0, Exactly(1));

  // Threads are killed on the first SIGKILL.
  EXPECT_CALL(*mock_tasks_handler_, ListThreads(TasksHandler::ListType::SELF))
      .WillOnce(Return(kTids))
      .WillRepeatedly(Return(vector<pid_t>()));
  ExpectKill(kTids, -1, Exactly(1));

  EXPECT_CALL(mock_kernel_.Mock(), Usleep(
      FLAGS_lmctfy_ms_delay_between_kills * 1000))
      .WillRepeatedly(Return(0));

  // We ignore signal failing.
  EXPECT_TRUE(container_->KillAll().ok());
}

TEST_F(ContainerImplTest, KillAllUnkillableTouristThreads) {
  const vector<pid_t> kPids = {1, 2, 3};
  const vector<pid_t> kTids = {4, 5, 6};

  // Processes are killed after the first SIGKILL.
  EXPECT_CALL(*mock_tasks_handler_, ListProcesses(TasksHandler::ListType::SELF))
      .WillOnce(Return(kPids))
      .WillRepeatedly(Return(vector<pid_t>()));
  ExpectKill(kPids, 0, Exactly(1));

  // Threads are never killed.
  EXPECT_CALL(*mock_tasks_handler_, ListThreads(TasksHandler::ListType::SELF))
      .WillRepeatedly(Return(kTids));
  ExpectKill(kTids, 0, AtLeast(1));

  EXPECT_CALL(mock_kernel_.Mock(), Usleep(
      FLAGS_lmctfy_ms_delay_between_kills * 1000))
      .WillRepeatedly(Return(0));

  Status status = container_->KillAll();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(::util::error::FAILED_PRECONDITION, status.error_code());
}

// Tests for KillTasks().

TEST_F(ContainerImplTest, KillTasksThreadsSuccess) {
  const vector<pid_t> kPids = {1, 2, 3};

  EXPECT_CALL(*mock_tasks_handler_, ListThreads(TasksHandler::ListType::SELF))
      .WillOnce(Return(kPids))
      .WillRepeatedly(Return(vector<pid_t>()));
  ExpectKill(kPids, 0, Exactly(1));

  EXPECT_CALL(mock_kernel_.Mock(), Usleep(
      FLAGS_lmctfy_ms_delay_between_kills * 1000))
      .WillRepeatedly(Return(0));

  EXPECT_TRUE(CallKillTasks(ContainerImpl::LIST_THREADS).ok());
}

TEST_F(ContainerImplTest, KillTasksThreadsOneFailure) {
  const vector<pid_t> kPids = {1, 2, 3};

  EXPECT_CALL(*mock_tasks_handler_, ListThreads(TasksHandler::ListType::SELF))
      .WillOnce(Return(kPids))
      .WillOnce(Return(kPids))
      .WillRepeatedly(Return(vector<pid_t>()));
  ExpectKill(kPids, 0, Exactly(2));

  EXPECT_CALL(mock_kernel_.Mock(), Usleep(
      FLAGS_lmctfy_ms_delay_between_kills * 1000))
      .WillRepeatedly(Return(0));

  EXPECT_TRUE(CallKillTasks(ContainerImpl::LIST_THREADS).ok());
}

TEST_F(ContainerImplTest, KillTasksThreadsTriesRunOut) {
  const vector<pid_t> kPids = {1, 2, 3};

  EXPECT_CALL(*mock_tasks_handler_, ListThreads(TasksHandler::ListType::SELF))
      .WillRepeatedly(Return(kPids));
  ExpectKill(kPids, 0, AtLeast(1));

  EXPECT_CALL(mock_kernel_.Mock(), Usleep(
      FLAGS_lmctfy_ms_delay_between_kills * 1000))
      .WillRepeatedly(Return(0));

  Status status = CallKillTasks(ContainerImpl::LIST_THREADS);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(::util::error::FAILED_PRECONDITION, status.error_code());
}

TEST_F(ContainerImplTest, KillTasksThreadsTriesRunOutListFails) {
  const vector<pid_t> kPids = {1, 2, 3};

  EXPECT_CALL(*mock_tasks_handler_, ListThreads(TasksHandler::ListType::SELF))
      .WillOnce(Return(kPids))
      .WillOnce(Return(kPids))
      .WillOnce(Return(kPids))
      .WillOnce(Return(Status::CANCELLED));
  ExpectKill(kPids, 0, Exactly(3));

  EXPECT_CALL(mock_kernel_.Mock(), Usleep(
      FLAGS_lmctfy_ms_delay_between_kills * 1000))
      .WillRepeatedly(Return(0));

  EXPECT_EQ(Status::CANCELLED, CallKillTasks(ContainerImpl::LIST_THREADS));
}

TEST_F(ContainerImplTest, KillTasksThreadsListFails) {
  const vector<pid_t> kPids = {1, 2, 3};

  EXPECT_CALL(*mock_tasks_handler_, ListThreads(TasksHandler::ListType::SELF))
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED, CallKillTasks(ContainerImpl::LIST_THREADS));
}

TEST_F(ContainerImplTest, KillTasksThreadsKillFails) {
  const vector<pid_t> kPids = {1, 2, 3};

  EXPECT_CALL(*mock_tasks_handler_, ListThreads(TasksHandler::ListType::SELF))
      .WillOnce(Return(kPids))
      .WillRepeatedly(Return(vector<pid_t>()));
  ExpectKill(kPids, -1, Exactly(1));

  EXPECT_CALL(mock_kernel_.Mock(), Usleep(
      FLAGS_lmctfy_ms_delay_between_kills * 1000))
      .WillRepeatedly(Return(0));

  // We ignore kill failing
  EXPECT_TRUE(CallKillTasks(ContainerImpl::LIST_THREADS).ok());
}

TEST_F(ContainerImplTest, KillTasksProcessesSuccess) {
  const vector<pid_t> kPids = {1, 2, 3};

  EXPECT_CALL(*mock_tasks_handler_, ListProcesses(TasksHandler::ListType::SELF))
      .WillOnce(Return(kPids))
      .WillRepeatedly(Return(vector<pid_t>()));
  ExpectKill(kPids, 0, Exactly(1));

  EXPECT_CALL(mock_kernel_.Mock(), Usleep(
      FLAGS_lmctfy_ms_delay_between_kills * 1000))
      .WillRepeatedly(Return(0));

  EXPECT_TRUE(CallKillTasks(ContainerImpl::LIST_PROCESSES).ok());
}

TEST_F(ContainerImplTest, KillTasksProcessesOneFailure) {
  const vector<pid_t> kPids = {1, 2, 3};

  EXPECT_CALL(*mock_tasks_handler_, ListProcesses(TasksHandler::ListType::SELF))
      .WillOnce(Return(kPids))
      .WillOnce(Return(kPids))
      .WillRepeatedly(Return(vector<pid_t>()));
  ExpectKill(kPids, 0, Exactly(2));

  EXPECT_CALL(mock_kernel_.Mock(), Usleep(
      FLAGS_lmctfy_ms_delay_between_kills * 1000))
      .WillRepeatedly(Return(0));

  EXPECT_TRUE(CallKillTasks(ContainerImpl::LIST_PROCESSES).ok());
}

TEST_F(ContainerImplTest, KillTasksProcessesTriesRunOut) {
  const vector<pid_t> kPids = {1, 2, 3};

  EXPECT_CALL(*mock_tasks_handler_, ListProcesses(TasksHandler::ListType::SELF))
      .WillRepeatedly(Return(kPids));
  ExpectKill(kPids, 0, AtLeast(1));

  EXPECT_CALL(mock_kernel_.Mock(), Usleep(
      FLAGS_lmctfy_ms_delay_between_kills * 1000))
      .WillRepeatedly(Return(0));

  Status status = CallKillTasks(ContainerImpl::LIST_PROCESSES);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(::util::error::FAILED_PRECONDITION, status.error_code());
}

TEST_F(ContainerImplTest, KillTasksProcessesTriesRunOutListFails) {
  const vector<pid_t> kPids = {1, 2, 3};

  EXPECT_CALL(*mock_tasks_handler_, ListProcesses(TasksHandler::ListType::SELF))
      .WillOnce(Return(kPids))
      .WillOnce(Return(kPids))
      .WillOnce(Return(kPids))
      .WillOnce(Return(Status::CANCELLED));
  ExpectKill(kPids, 0, Exactly(3));

  EXPECT_CALL(mock_kernel_.Mock(), Usleep(
      FLAGS_lmctfy_ms_delay_between_kills * 1000))
      .WillRepeatedly(Return(0));

  EXPECT_EQ(Status::CANCELLED, CallKillTasks(ContainerImpl::LIST_PROCESSES));
}

TEST_F(ContainerImplTest, KillTasksProcessesListFails) {
  const vector<pid_t> kPids = {1, 2, 3};

  EXPECT_CALL(*mock_tasks_handler_, ListProcesses(TasksHandler::ListType::SELF))
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED, CallKillTasks(ContainerImpl::LIST_PROCESSES));
}

TEST_F(ContainerImplTest, KillTasksProcessesKillFails) {
  const vector<pid_t> kPids = {1, 2, 3};

  EXPECT_CALL(*mock_tasks_handler_, ListProcesses(TasksHandler::ListType::SELF))
      .WillOnce(Return(kPids))
      .WillRepeatedly(Return(vector<pid_t>()));
  ExpectKill(kPids, -1, Exactly(1));

  EXPECT_CALL(mock_kernel_.Mock(), Usleep(
      FLAGS_lmctfy_ms_delay_between_kills * 1000))
      .WillRepeatedly(Return(0));

  // We ignore kill failing
  EXPECT_TRUE(CallKillTasks(ContainerImpl::LIST_PROCESSES).ok());
}

// Tests for Enter().

TEST_F(ContainerImplTest, EnterNoContainer) {
  ExpectExists(false);

  EXPECT_ERROR_CODE(NOT_FOUND, container_->Enter({0}));
}

TEST_F(ContainerImplTest, EnterSuccess) {
  vector<pid_t> tids = {1, 2, 3};

  MockNamespaceHandler *namespace_handler = ExpectGetNamespaceHandler();
  EXPECT_CALL(*namespace_handler, IsDifferentVirtualHost(tids))
      .WillRepeatedly(Return(false));

  // Enter() takes ownership of the return of GetResourceHandlers().
  vector<MockResourceHandler *> handlers = ExpectGetResourceHandlers(
      Status::OK);

  EXPECT_CALL(*mock_freezer_controller_, Enter(_))
      .Times(3)
      .WillRepeatedly(Return(Status::OK));

  EXPECT_CALL(*mock_tasks_handler_, TrackTasks(tids))
      .WillRepeatedly(Return(Status::OK));

  for (MockResourceHandler *handler : handlers) {
    EXPECT_CALL(*handler, Enter(tids))
        .WillRepeatedly(Return(Status::OK));
  }

  EXPECT_TRUE(container_->Enter(tids).ok());
}

TEST_F(ContainerImplTest, EnterNoTids) {
  vector<pid_t> tids;

  MockNamespaceHandler *namespace_handler = ExpectGetNamespaceHandler();
  EXPECT_CALL(*namespace_handler, IsDifferentVirtualHost(tids))
      .WillRepeatedly(Return(false));

  EXPECT_CALL(*mock_tasks_handler_, TrackTasks(tids))
      .WillRepeatedly(Return(Status::OK));

  // Enter() takes ownership of the return of GetResourceHandlers().
  vector<MockResourceHandler *> handlers = ExpectGetResourceHandlers(
      Status::OK);

  for (MockResourceHandler *handler : handlers) {
    EXPECT_CALL(*handler, Enter(tids))
        .WillRepeatedly(Return(Status::OK));
  }

  EXPECT_TRUE(container_->Enter(tids).ok());
}

TEST_F(ContainerImplTest, EnterTrackTasksFails) {
  vector<pid_t> tids = {1, 2, 3};

  MockNamespaceHandler *namespace_handler = ExpectGetNamespaceHandler();
  EXPECT_CALL(*namespace_handler, IsDifferentVirtualHost(tids))
      .WillRepeatedly(Return(false));

  // Enter() takes ownership of the return of GetResourceHandlers().
  vector<MockResourceHandler *> handlers = ExpectGetResourceHandlers(
      Status::OK);

  EXPECT_CALL(*mock_freezer_controller_, Enter(_))
      .Times(3)
      .WillRepeatedly(Return(Status::OK));

  EXPECT_CALL(*mock_tasks_handler_, TrackTasks(tids))
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED, container_->Enter(tids));
}

TEST_F(ContainerImplTest, EnterGetResourceHandlersFails) {
  vector<pid_t> tids = {1, 2, 3};

  MockNamespaceHandler *namespace_handler = ExpectGetNamespaceHandler();
  EXPECT_CALL(*namespace_handler, IsDifferentVirtualHost(tids))
      .WillRepeatedly(Return(false));

  // Enter() takes ownership of the return of GetResourceHandlers().
  ExpectGetResourceHandlers(Status::CANCELLED);

  EXPECT_EQ(Status::CANCELLED, container_->Enter(tids));
}

TEST_F(ContainerImplTest, Enter_FreezerEnter_Fails) {
  vector<pid_t> tids = {1, 2, 3};

  MockNamespaceHandler *namespace_handler = ExpectGetNamespaceHandler();
  EXPECT_CALL(*namespace_handler, IsDifferentVirtualHost(tids))
      .WillRepeatedly(Return(false));

  // Enter() takes ownership of the return of GetResourceHandlers().
  vector<MockResourceHandler *> handlers = ExpectGetResourceHandlers(
      Status::OK);

  EXPECT_CALL(*mock_freezer_controller_, Enter(_))
      .WillOnce(Return(Status::OK))
      .WillOnce(Return(Status::OK))
      .WillOnce(Return(Status::CANCELLED));

  EXPECT_ERROR_CODE(::util::error::CANCELLED, container_->Enter(tids));
}

TEST_F(ContainerImplTest, EnterResourceHandlerEnterFails) {
  vector<pid_t> tids = {1, 2, 3};

  MockNamespaceHandler *namespace_handler = ExpectGetNamespaceHandler();
  EXPECT_CALL(*namespace_handler, IsDifferentVirtualHost(tids))
      .WillRepeatedly(Return(false));

  EXPECT_CALL(*mock_freezer_controller_, Enter(_))
      .Times(3)
      .WillRepeatedly(Return(Status::OK));

  EXPECT_CALL(*mock_tasks_handler_, TrackTasks(tids))
      .WillRepeatedly(Return(Status::OK));

  // Enter() takes ownership of the return of GetResourceHandlers().
  vector<MockResourceHandler *> handlers = ExpectGetResourceHandlers(
      Status::OK);
  for (MockResourceHandler *handler : handlers) {
    EXPECT_CALL(*handler, Enter(tids))
        .WillRepeatedly(Return(Status::CANCELLED));
  }

  EXPECT_EQ(Status::CANCELLED, container_->Enter(tids));
}

TEST_F(ContainerImplTest, EnterDifferentVirtualHost) {
  vector<pid_t> tids = {1, 2, 3};

  MockNamespaceHandler *namespace_handler = ExpectGetNamespaceHandler();
  EXPECT_CALL(*namespace_handler, IsDifferentVirtualHost(tids))
      .WillRepeatedly(Return(true));

  EXPECT_ERROR_CODE(FAILED_PRECONDITION, container_->Enter(tids));
}

// Tests for Destroy().

TEST_F(ContainerImplTest, DestroyNoContainer) {
  ExpectExists(false);

  EXPECT_ERROR_CODE(NOT_FOUND, container_->Destroy());
}

TEST_F(ContainerImplTest, DestroySuccess) {
  ExpectSimpleKillAll(Status::OK);

  // All attached resources are destroyed (all but Global)
  vector<MockResourceHandler *> mock_handlers =
      ExpectGetResourceHandlers(Status::OK);
  vector<MockResourceHandler *> to_delete;
  for (MockResourceHandler *mock_handler : mock_handlers) {
    if (mock_handler->type() != RESOURCE_FILESYSTEM) {
      EXPECT_CALL(*mock_handler, Destroy())
          .WillOnce(Return(Status::OK));
      to_delete.push_back(mock_handler);
    }
  }
  unique_ptr<MockNamespaceHandler> mock_namespace_handler(
      ExpectGetNamespaceHandler());
  EXPECT_CALL(*mock_namespace_handler, Destroy())
      .WillOnce(Return(Status::OK));

  EXPECT_CALL(*mock_tasks_handler_, Destroy())
      .WillOnce(Return(Status::OK));

  EXPECT_CALL(*mock_freezer_controller_, Destroy())
      .WillOnce(Return(Status::OK));

  // Destroy() should delete the handlers.
  unique_ptr<MockTasksHandler> d1(mock_tasks_handler_);
  unique_ptr<MockFreezerController> d2(mock_freezer_controller_);
  ElementDeleter d(&to_delete);

  EXPECT_TRUE(container_->Destroy().ok());
}

TEST_F(ContainerImplTest, DestroyOnlyDestroyResourcesAttachedToContainer) {
  ExpectSimpleKillAll(Status::OK);

  // Two resources are attached to this container, the other to the parent
  // container.
  MockResourceHandler *mock_handler1 = new StrictMockResourceHandler(
      kContainerName, RESOURCE_CPU);
  EXPECT_CALL(*mock_resource_factory1_, Get(kContainerName))
      .WillRepeatedly(Return(mock_handler1));
  MockResourceHandler *mock_handler2 = new StrictMockResourceHandler(
      kParentContainer, RESOURCE_MEMORY);
  EXPECT_CALL(*mock_resource_factory2_, Get(kContainerName))
      .WillRepeatedly(Return(Status(::util::error::NOT_FOUND, "")));
  EXPECT_CALL(*mock_resource_factory2_, Get(kParentContainer))
      .WillRepeatedly(Return(mock_handler2));
  MockResourceHandler *mock_handler3 = new StrictMockResourceHandler(
      kContainerName, RESOURCE_FILESYSTEM);
  EXPECT_CALL(*mock_resource_factory3_, Get(kContainerName))
      .WillRepeatedly(Return(mock_handler3));

  // Only the attached resource handlers should be destroyed.
  EXPECT_CALL(*mock_handler1, Destroy())
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_handler3, Destroy())
      .WillOnce(Return(Status::OK));

  unique_ptr<MockNamespaceHandler> mock_namespace_handler(
      ExpectGetNamespaceHandler());
  EXPECT_CALL(*mock_namespace_handler, Destroy())
      .WillOnce(Return(Status::OK));

  EXPECT_CALL(*mock_tasks_handler_, Destroy())
      .WillOnce(Return(Status::OK));

  EXPECT_CALL(*mock_freezer_controller_, Destroy())
      .WillOnce(Return(Status::OK));

  // Destroy() should delete the handlers.
  unique_ptr<MockTasksHandler> d1(mock_tasks_handler_);
  unique_ptr<MockResourceHandler> d2(mock_handler1);
  unique_ptr<MockResourceHandler> d3(mock_handler3);
  unique_ptr<MockFreezerController> d4(mock_freezer_controller_);

  EXPECT_TRUE(container_->Destroy().ok());
}

TEST_F(ContainerImplTest, DestroyKillAllFails) {
  ExpectSimpleKillAll(Status::CANCELLED);

  EXPECT_EQ(Status::CANCELLED, container_->Destroy());
}

TEST_F(ContainerImplTest, DestroyGetResourceHandlersFails) {
  ExpectSimpleKillAll(Status::OK);

  vector<MockResourceHandler *> mock_handlers =
      ExpectGetResourceHandlers(Status::CANCELLED);
  ExpectGetNamespaceHandler();

  EXPECT_EQ(Status::CANCELLED, container_->Destroy());
}

TEST_F(ContainerImplTest, DestroyResourceHandlerDestroyFails) {
  ExpectSimpleKillAll(Status::OK);

  vector<MockResourceHandler *> mock_handlers =
      ExpectGetResourceHandlers(Status::OK);
  for (MockResourceHandler *mock_handler : mock_handlers) {
    EXPECT_CALL(*mock_handler, Destroy())
        .WillRepeatedly(Return(Status::CANCELLED));
  }
  MockNamespaceHandler *mock_namespace_handler = ExpectGetNamespaceHandler();
  EXPECT_CALL(*mock_namespace_handler, Destroy())
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED, container_->Destroy());
}

TEST_F(ContainerImplTest, DestroyTasksHandlerDestroyFails) {
  ExpectSimpleKillAll(Status::OK);

  vector<MockResourceHandler *> mock_handlers =
      ExpectGetResourceHandlers(Status::OK);
  vector<MockResourceHandler *> to_delete;
  for (MockResourceHandler *mock_handler : mock_handlers) {
    if (mock_handler->type() != RESOURCE_FILESYSTEM) {
      EXPECT_CALL(*mock_handler, Destroy())
          .WillOnce(Return(Status::OK));
      to_delete.push_back(mock_handler);
    }
  }
  unique_ptr<MockNamespaceHandler> mock_namespace_handler(
      ExpectGetNamespaceHandler());
  EXPECT_CALL(*mock_namespace_handler, Destroy())
      .WillRepeatedly(Return(Status::OK));
  // Handlers are deleted by Destroy().
  ElementDeleter d(&to_delete);

  EXPECT_CALL(*mock_tasks_handler_, Destroy())
      .WillOnce(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED, container_->Destroy());
}


TEST_F(ContainerImplTest, Destroy_FreezerController_Destroy_Fails) {
  ExpectSimpleKillAll(Status::OK);

  // All attached resources are destroyed (all but Global)
  vector<MockResourceHandler *> mock_handlers =
      ExpectGetResourceHandlers(Status::OK);
  vector<MockResourceHandler *> to_delete;
  for (MockResourceHandler *mock_handler : mock_handlers) {
    if (mock_handler->type() != RESOURCE_FILESYSTEM) {
      EXPECT_CALL(*mock_handler, Destroy())
          .WillOnce(Return(Status::OK));
      to_delete.push_back(mock_handler);
    }
  }
  unique_ptr<MockNamespaceHandler> mock_namespace_handler(
      ExpectGetNamespaceHandler());
  EXPECT_CALL(*mock_namespace_handler, Destroy())
      .WillRepeatedly(Return(Status::OK));

  EXPECT_CALL(*mock_tasks_handler_, Destroy())
      .WillOnce(Return(Status::OK));

  EXPECT_CALL(*mock_freezer_controller_, Destroy())
      .WillOnce(Return(Status::CANCELLED));

  // Destroy() should delete the handlers.
  unique_ptr<MockTasksHandler> d1(mock_tasks_handler_);
  ElementDeleter d(&to_delete);

  EXPECT_ERROR_CODE(::util::error::CANCELLED, container_->Destroy());
}

// Tests for Run().

static const pid_t kPid = 22;

TEST_F(ContainerImplTest, RunNoCommand) {
  const vector<string> kCmd = {};
  const vector<RunSpec::FdPolicy> kFdPolicies = {RunSpec::INHERIT,
                                                 RunSpec::DETACHED};

  for (const auto &fd_policy : kFdPolicies) {
    ExpectExists(true);

    RunSpec spec;
    spec.set_fd_policy(fd_policy);
    EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT,
                      container_->Run(kCmd, spec));
  }
}

TEST_F(ContainerImplTest, RunUnknownFdPolicy) {
  ExpectExists(true);

  RunSpec spec;
  spec.set_fd_policy(RunSpec::UNKNOWN);
  EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT,
                    container_->Run({"/bin/true"}, spec));
}

TEST_F(ContainerImplTest, RunNoContainer) {
  const vector<string> kCmd = {"/bin/echo", "test", "cmd"};
  const vector<RunSpec::FdPolicy> kFdPolicies = {RunSpec::INHERIT,
                                                 RunSpec::DETACHED};

  for (const auto &fd_policy : kFdPolicies) {
    ExpectExists(false);

    RunSpec spec;
    spec.set_fd_policy(fd_policy);
    EXPECT_ERROR_CODE(NOT_FOUND, container_->Run(kCmd, spec));
  }
}

TEST_F(ContainerImplTest, RunSuccessBackground) {
  const vector<string> kCmd = {"/bin/echo", "test", "cmd"};
  RunSpec spec;
  spec.set_fd_policy(RunSpec::DETACHED);

  MockNamespaceHandler *mock_namespace_handler =
      new StrictMockNamespaceHandler(kContainerName, RESOURCE_VIRTUALHOST);
  EXPECT_CALL(*mock_namespace_handler_factory_,
              GetNamespaceHandler(kContainerName))
      .WillOnce(Return(mock_namespace_handler));
  EXPECT_CALL(*mock_namespace_handler, Run(kCmd, EqualsInitializedProto(spec)))
      .WillOnce(Return(kPid));
  ExpectEnterInto(0, Status::OK);

  StatusOr<pid_t> statusor = container_->Run(kCmd, spec);
  ASSERT_TRUE(statusor.ok());
  EXPECT_EQ(kPid, statusor.ValueOrDie());
}

TEST_F(ContainerImplTest, RunSuccessForeground) {
  const vector<string> kCmd = {"/bin/echo", "test", "cmd"};
  RunSpec spec;
  spec.set_fd_policy(RunSpec::INHERIT);

  MockNamespaceHandler *mock_namespace_handler =
      new StrictMockNamespaceHandler(kContainerName, RESOURCE_VIRTUALHOST);
  EXPECT_CALL(*mock_namespace_handler_factory_,
              GetNamespaceHandler(kContainerName))
      .WillOnce(Return(mock_namespace_handler));
  EXPECT_CALL(*mock_namespace_handler, Run(kCmd, EqualsInitializedProto(spec)))
      .WillOnce(Return(kPid));
  ExpectEnterInto(0, Status::OK);

  StatusOr<pid_t> statusor = container_->Run(kCmd, spec);
  ASSERT_TRUE(statusor.ok());
  EXPECT_EQ(kPid, statusor.ValueOrDie());
}

TEST_F(ContainerImplTest, RunSuccessDefaultPolicy) {
  const vector<string> kCmd = {"/bin/echo", "test", "cmd"};
  // Inherit is the default policy.
  RunSpec spec;

  MockNamespaceHandler *mock_namespace_handler =
      new StrictMockNamespaceHandler(kContainerName, RESOURCE_VIRTUALHOST);
  EXPECT_CALL(*mock_namespace_handler_factory_,
              GetNamespaceHandler(kContainerName))
      .WillOnce(Return(mock_namespace_handler));
  EXPECT_CALL(*mock_namespace_handler, Run(kCmd, EqualsInitializedProto(spec)))
      .WillOnce(Return(kPid));
  ExpectEnterInto(0, Status::OK);

  StatusOr<pid_t> statusor = container_->Run(kCmd, spec);
  ASSERT_TRUE(statusor.ok());
  EXPECT_EQ(kPid, statusor.ValueOrDie());
}

TEST_F(ContainerImplTest, RunEnterFails) {
  const vector<string> kCmd = {"/bin/echo", "test", "cmd"};

  ExpectEnterInto(0, Status::CANCELLED);

  RunSpec spec;
  spec.set_fd_policy(RunSpec::DETACHED);
  EXPECT_EQ(Status::CANCELLED, container_->Run(kCmd, spec).status());
}

TEST_F(ContainerImplTest, NamespaceRunFails) {
  const vector<string> kCmd = {"/bin/echo", "test", "cmd"};
  RunSpec spec;
  spec.set_fd_policy(RunSpec::DETACHED);

  MockNamespaceHandler *mock_namespace_handler =
      new StrictMockNamespaceHandler(kContainerName, RESOURCE_VIRTUALHOST);
  EXPECT_CALL(*mock_namespace_handler_factory_,
              GetNamespaceHandler(kContainerName))
      .WillOnce(Return(mock_namespace_handler));
  EXPECT_CALL(*mock_namespace_handler, Run(kCmd, EqualsInitializedProto(spec)))
      .WillOnce(Return(Status(::util::error::FAILED_PRECONDITION, "")));

  ExpectEnterInto(0, Status::OK);

  StatusOr<pid_t> statusor = container_->Run(kCmd, spec);
  EXPECT_FALSE(statusor.ok());
  EXPECT_EQ(::util::error::FAILED_PRECONDITION, statusor.status().error_code());
}

// Tests for Update().

TEST_F(ContainerImplTest, UpdateNoContainer) {
  const vector<Container::UpdatePolicy> kUpdatePolicies = {
      Container::UPDATE_DIFF, Container::UPDATE_REPLACE};

  ContainerSpec spec;
  for (const auto &update_policy : kUpdatePolicies) {
    ExpectExists(false);

    EXPECT_ERROR_CODE(NOT_FOUND, container_->Update(spec, update_policy));
  }
}

TEST_F(ContainerImplTest, UpdateDiffSuccess) {
  ContainerSpec spec;
  spec.mutable_cpu()->set_limit(2000);
  spec.mutable_memory()->set_limit(100);

  // Returns CPU, MEMORY, and FILESYSTEM ResourceHandlers.
  vector<MockResourceHandler *> mock_handlers = ExpectGetResourceHandlers(
      Status::OK);
  ExpectGetNamespaceHandler();

  // Expect all resources of this container to be updated (filesystem is not in
  // this container).
  for (MockResourceHandler *mock_handler : mock_handlers) {
    if (mock_handler->type() != RESOURCE_FILESYSTEM) {
      EXPECT_CALL(*mock_handler,
                  Update(EqualsInitializedProto(spec), Container::UPDATE_DIFF))
          .WillOnce(Return(Status::OK));
    }
  }

  EXPECT_TRUE(container_->Update(spec, Container::UPDATE_DIFF).ok());
}

TEST_F(ContainerImplTest, UpdateDiffOnlyMemoryUpdated) {
  ContainerSpec spec;
  spec.mutable_memory()->set_limit(100);

  // Returns CPU, MEMORY, and FILESYSTEM ResourceHandlers.
  vector<MockResourceHandler *> mock_handlers = ExpectGetResourceHandlers(
      Status::OK);
  ExpectGetNamespaceHandler();

  // Expect only memory to be updated.
  for (MockResourceHandler *mock_handler : mock_handlers) {
    if (mock_handler->type() == RESOURCE_MEMORY) {
      EXPECT_CALL(*mock_handler,
                  Update(EqualsInitializedProto(spec), Container::UPDATE_DIFF))
          .WillOnce(Return(Status::OK));
    }
  }

  EXPECT_TRUE(container_->Update(spec, Container::UPDATE_DIFF).ok());
}

TEST_F(ContainerImplTest, UpdateDiffEmptySpec) {
  ContainerSpec spec;

  // Returns CPU, MEMORY, and FILESYSTEM ResourceHandlers.
  vector<MockResourceHandler *> mock_handlers = ExpectGetResourceHandlers(
      Status::OK);
  ExpectGetNamespaceHandler();

  // None should be updated.

  EXPECT_TRUE(container_->Update(spec, Container::UPDATE_DIFF).ok());
}

TEST_F(ContainerImplTest, UpdateDiffNonExistingResource) {
  ContainerSpec spec;

  // Specify filesystem which is not attached to this container.
  spec.mutable_filesystem()->set_fd_limit(100);

  // Returns CPU, MEMORY, and FILESYSTEM ResourceHandlers.
  ExpectGetResourceHandlers(Status::OK);
  ExpectGetNamespaceHandler();

  Status status = container_->Update(spec, Container::UPDATE_DIFF);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(::util::error::INVALID_ARGUMENT, status.error_code());
}

TEST_F(ContainerImplTest, UpdateDiffGetResourceHandlersFails) {
  ContainerSpec spec;

  ExpectGetResourceHandlers(Status::CANCELLED);
  ExpectGetNamespaceHandler();

  EXPECT_EQ(Status::CANCELLED,
            container_->Update(spec, Container::UPDATE_DIFF));
}

TEST_F(ContainerImplTest, UpdateDiffUpdateFails) {
  ContainerSpec spec;
  spec.mutable_cpu()->set_limit(2000);
  spec.mutable_memory()->set_limit(100);

  // Returns CPU, MEMORY, and FILESYSTEM ResourceHandlers.
  vector<MockResourceHandler *> mock_handlers = ExpectGetResourceHandlers(
      Status::OK);
  ExpectGetNamespaceHandler();

  // Expect all to be updated.
  for (MockResourceHandler *mock_handler : mock_handlers) {
    EXPECT_CALL(*mock_handler,
                Update(EqualsInitializedProto(spec), Container::UPDATE_DIFF))
        .WillRepeatedly(Return(Status::CANCELLED));
  }

  EXPECT_EQ(Status::CANCELLED,
            container_->Update(spec, Container::UPDATE_DIFF));
}

TEST_F(ContainerImplTest, UpdateReplaceSuccess) {
  ContainerSpec spec;
  spec.mutable_cpu()->set_limit(2000);
  spec.mutable_memory()->set_limit(100);
  spec.mutable_virtual_host();

  // Returns CPU, MEMORY, and FILESYSTEM ResourceHandlers.
  vector<MockResourceHandler *> mock_handlers = ExpectGetResourceHandlers(
      Status::OK);
  MockNamespaceHandler *mock_namespace_handler = ExpectGetNamespaceHandler();

  // Expect all resources of this container to be updated (filesystem is not in
  // this container).
  for (MockResourceHandler *mock_handler : mock_handlers) {
    if (mock_handler->type() != RESOURCE_FILESYSTEM) {
      EXPECT_CALL(*mock_handler, Update(EqualsInitializedProto(spec),
                                        Container::UPDATE_REPLACE))
          .WillOnce(Return(Status::OK));
    }
  }
  EXPECT_CALL(*mock_namespace_handler, Update(EqualsInitializedProto(spec),
                                              Container::UPDATE_REPLACE))
      .WillOnce(Return(Status::OK));

  EXPECT_TRUE(container_->Update(spec, Container::UPDATE_REPLACE).ok());
}

TEST_F(ContainerImplTest, UpdateReplaceSomeResourcesMissing) {
  ContainerSpec spec;
  spec.mutable_cpu()->set_limit(2000);

  // Returns CPU, MEMORY, and FILESYSTEM ResourceHandlers.
  vector<MockResourceHandler *> mock_handlers = ExpectGetResourceHandlers(
      Status::OK);
  ExpectGetNamespaceHandler();

  EXPECT_FALSE(container_->Update(spec, Container::UPDATE_REPLACE).ok());
}

TEST_F(ContainerImplTest, UpdateReplaceSomeResourcesExtra) {
  ContainerSpec spec;
  spec.mutable_cpu()->set_limit(2000);
  spec.mutable_memory()->set_limit(100);
  spec.mutable_filesystem();

  // Returns CPU, MEMORY, and FILESYSTEM ResourceHandlers.
  vector<MockResourceHandler *> mock_handlers = ExpectGetResourceHandlers(
      Status::OK);
  ExpectGetNamespaceHandler();

  EXPECT_FALSE(container_->Update(spec, Container::UPDATE_REPLACE).ok());
}

TEST_F(ContainerImplTest, UpdateReplaceSomeResourcesExtraAndSomeMissing) {
  ContainerSpec spec;
  spec.mutable_cpu()->set_limit(2000);
  spec.mutable_filesystem();

  // Returns CPU, MEMORY, and FILESYSTEM ResourceHandlers.
  vector<MockResourceHandler *> mock_handlers = ExpectGetResourceHandlers(
      Status::OK);
  ExpectGetNamespaceHandler();

  EXPECT_FALSE(container_->Update(spec, Container::UPDATE_REPLACE).ok());
}

TEST_F(ContainerImplTest, UpdateReplaceGetResourceHandlersFails) {
  ContainerSpec spec;

  ExpectGetResourceHandlers(Status::CANCELLED);
  ExpectGetNamespaceHandler();

  EXPECT_EQ(Status::CANCELLED,
            container_->Update(spec, Container::UPDATE_REPLACE));
}

TEST_F(ContainerImplTest, UpdateReplaceUpdateFails) {
  ContainerSpec spec;
  spec.mutable_cpu()->set_limit(2000);
  spec.mutable_memory()->set_limit(100);
  spec.mutable_virtual_host();

  // Returns CPU, MEMORY, and FILESYSTEM ResourceHandlers.
  vector<MockResourceHandler *> mock_handlers = ExpectGetResourceHandlers(
      Status::OK);
  MockNamespaceHandler *mock_namespace_handler = ExpectGetNamespaceHandler();

  // Expect all resources of this container to be updated (filesystem is not in
  // this container).
  for (MockResourceHandler *mock_handler : mock_handlers) {
    if (mock_handler->type() != RESOURCE_FILESYSTEM) {
      EXPECT_CALL(*mock_handler, Update(EqualsInitializedProto(spec),
                                        Container::UPDATE_REPLACE))
          .WillRepeatedly(Return(Status::CANCELLED));
    }
  }
  EXPECT_CALL(*mock_namespace_handler, Update(EqualsInitializedProto(spec),
                                              Container::UPDATE_REPLACE))
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED,
            container_->Update(spec, Container::UPDATE_REPLACE));
}

// Tests for RegisterNotification().

// Dummy callback for notifications.
void NotificationCallback(Container *container, Status status) {
  EXPECT_TRUE(false) << "Should never be called";
}

// Deletes the specified callback.
void DeleteCallback(const EventSpec &spec, Callback1<Status> *callback) {
  delete callback;
}

TEST_F(ContainerImplTest, RegisterNotificationNoContainer) {
  EventSpec spec;
  spec.mutable_oom();

  ExpectExists(false);

  EXPECT_ERROR_CODE(NOT_FOUND,
                    container_->RegisterNotification(
                        spec, NewPermanentCallback(&NotificationCallback)));
}

TEST_F(ContainerImplTest, RegisterNotificationSuccess) {
  EventSpec spec;
  spec.mutable_oom();

  // Returns CPU, MEMORY, and FILESYSTEM ResourceHandlers.
  vector<MockResourceHandler *> mock_handlers = ExpectGetResourceHandlers(
      Status::OK);
  ExpectGetNamespaceHandler();

  // Memory should register a notification, all else should not know about this
  // event.
  for (MockResourceHandler *mock_handler : mock_handlers) {
    if (mock_handler->type() == RESOURCE_MEMORY) {
      EXPECT_CALL(*mock_handler,
                  RegisterNotification(EqualsInitializedProto(spec), NotNull()))
          .WillRepeatedly(DoAll(Invoke(&DeleteCallback), Return(1)));
    } else {
      EXPECT_CALL(*mock_handler,
                  RegisterNotification(EqualsInitializedProto(spec), NotNull()))
          .WillRepeatedly(DoAll(Invoke(&DeleteCallback),
                                Return(Status(::util::error::NOT_FOUND, ""))));
    }
  }

  StatusOr<Container::NotificationId> statusor =
      container_->RegisterNotification(
          spec, NewPermanentCallback(&NotificationCallback));
  EXPECT_OK(statusor);
  EXPECT_EQ(1, statusor.ValueOrDie());
}

TEST_F(ContainerImplTest, RegisterNotificationBadCallback) {
  EventSpec spec;
  spec.mutable_oom();

  EXPECT_DEATH(container_->RegisterNotification(spec, nullptr),
               "Must be non NULL");
  EXPECT_DEATH(container_->RegisterNotification(
                   spec, NewCallback(&NotificationCallback)),
               "not a repeatable callback");
}

TEST_F(ContainerImplTest, RegisterNotificationGetResourceHandlersFails) {
  EventSpec spec;
  spec.mutable_oom();

  // Returns CPU, MEMORY, and FILESYSTEM ResourceHandlers.
  vector<MockResourceHandler *> mock_handlers = ExpectGetResourceHandlers(
      Status::CANCELLED);
  ExpectGetNamespaceHandler();

  EXPECT_ERROR_CODE(::util::error::CANCELLED,
                    container_->RegisterNotification(
                        spec, NewPermanentCallback(&NotificationCallback)));
}

TEST_F(ContainerImplTest, RegisterNotificationFailureWhileRegistering) {
  EventSpec spec;
  spec.mutable_oom();

  // Returns CPU, MEMORY, and FILESYSTEM ResourceHandlers.
  vector<MockResourceHandler *> mock_handlers = ExpectGetResourceHandlers(
      Status::OK);
  MockNamespaceHandler *mock_namespace_handler = ExpectGetNamespaceHandler();

  // Memory has an error while registering the notification.
  for (MockResourceHandler *mock_handler : mock_handlers) {
    if (mock_handler->type() == RESOURCE_MEMORY) {
      EXPECT_CALL(*mock_handler,
                  RegisterNotification(EqualsInitializedProto(spec), NotNull()))
          .WillRepeatedly(
               DoAll(Invoke(&DeleteCallback), Return(Status::CANCELLED)));
    } else {
      EXPECT_CALL(*mock_handler,
                  RegisterNotification(EqualsInitializedProto(spec), NotNull()))
          .WillRepeatedly(DoAll(Invoke(&DeleteCallback),
                                Return(Status(::util::error::NOT_FOUND, ""))));
    }
  }
  EXPECT_CALL(*mock_namespace_handler,
              RegisterNotification(EqualsInitializedProto(spec), NotNull()))
      .WillRepeatedly(DoAll(Invoke(&DeleteCallback),
                            Return(Status(::util::error::NOT_FOUND, ""))));

  EXPECT_ERROR_CODE(::util::error::CANCELLED,
                    container_->RegisterNotification(
                        spec, NewPermanentCallback(&NotificationCallback)));
}

TEST_F(ContainerImplTest, RegisterNotificationNoHandlerForEvent) {
  EventSpec spec;
  spec.mutable_oom();

  // Returns CPU, MEMORY, and FILESYSTEM ResourceHandlers.
  vector<MockResourceHandler *> mock_handlers = ExpectGetResourceHandlers(
      Status::OK);
  MockNamespaceHandler *mock_namespace_handler = ExpectGetNamespaceHandler();

  // No handler can register a notification for the event.
  for (MockResourceHandler *mock_handler : mock_handlers) {
    EXPECT_CALL(*mock_handler,
                RegisterNotification(EqualsInitializedProto(spec), NotNull()))
        .WillRepeatedly(DoAll(Invoke(&DeleteCallback),
                              Return(Status(::util::error::NOT_FOUND, ""))));
  }
  EXPECT_CALL(*mock_namespace_handler,
              RegisterNotification(EqualsInitializedProto(spec), NotNull()))
      .WillRepeatedly(DoAll(Invoke(&DeleteCallback),
                            Return(Status(::util::error::NOT_FOUND, ""))));

  EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT,
                    container_->RegisterNotification(
                        spec, NewPermanentCallback(&NotificationCallback)));
}

// Tests for UnregisterNotification().

TEST_F(ContainerImplTest, UnregisterNotificationNoContainer) {
  ExpectExists(false);

  // Register the notification.
  Container::NotificationId notification_id = active_notifications_.Add();

  EXPECT_ERROR_CODE(NOT_FOUND,
                    container_->UnregisterNotification(notification_id));
}

TEST_F(ContainerImplTest, UnregisterNotificationSuccess) {
  // Register the notification.
  Container::NotificationId notification_id = active_notifications_.Add();

  EXPECT_OK(container_->UnregisterNotification(notification_id));
}

TEST_F(ContainerImplTest, UnregisterNotificationAlreadyUnregistered) {
  // Register the notification and then unregister it.
  Container::NotificationId notification_id = active_notifications_.Add();
  ASSERT_OK(container_->UnregisterNotification(notification_id));

  // Already unregistered, should not be able to unregister it again.
  EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT,
                    container_->UnregisterNotification(notification_id));
}

// Tests for Spec().

TEST_F(ContainerImplTest, SpecNoContainer) {
  ExpectExists(false);

  EXPECT_ERROR_CODE(NOT_FOUND, container_->Spec());
}

// Append the specified type to the output. Does not actually fill with values.
void AppendCpuSpec(ContainerSpec *output) {
  output->mutable_cpu();
}
void AppendMemorySpec(ContainerSpec *output) {
  output->mutable_memory();
}

TEST_F(ContainerImplTest, SpecSuccess) {
  vector<MockResourceHandler *> mock_handlers =
      ExpectGetResourceHandlers(Status::OK);
  MockNamespaceHandler *mock_namespace_handler = ExpectGetNamespaceHandler();

  // Add Spec() to CPU and memory, others are not attached to this container..
  for (MockResourceHandler *mock_handler : mock_handlers) {
    if (mock_handler->type() == RESOURCE_CPU) {
      EXPECT_CALL(*mock_handler, Spec(NotNull()))
          .WillOnce(DoAll(Invoke(&AppendCpuSpec), Return(Status::OK)));
    } else if (mock_handler->type() == RESOURCE_MEMORY) {
      EXPECT_CALL(*mock_handler, Spec(NotNull()))
          .WillOnce(DoAll(Invoke(&AppendMemorySpec), Return(Status::OK)));
    }
  }
  EXPECT_CALL(*mock_namespace_handler, Spec(NotNull()))
      .WillOnce(Return(Status::OK));

  StatusOr<ContainerSpec> statusor = container_->Spec();
  ASSERT_OK(statusor);
  ContainerSpec stats = statusor.ValueOrDie();
  EXPECT_TRUE(stats.has_cpu());
  EXPECT_TRUE(stats.has_memory());
  EXPECT_FALSE(stats.has_blockio());
  EXPECT_FALSE(stats.has_network());
  EXPECT_FALSE(stats.has_monitoring());
  EXPECT_FALSE(stats.has_filesystem());
}

TEST_F(ContainerImplTest, SpecEmpty) {
  vector<MockResourceHandler *> mock_handlers =
      ExpectGetResourceHandlers(Status::OK);
  MockNamespaceHandler *mock_namespace_handler = ExpectGetNamespaceHandler();

  for (MockResourceHandler *mock_handler : mock_handlers) {
    if ((mock_handler->type() == RESOURCE_CPU) ||
        (mock_handler->type() == RESOURCE_MEMORY)) {
      EXPECT_CALL(*mock_handler, Spec(NotNull()))
          .WillOnce(Return(Status::OK));
    }
  }
  EXPECT_CALL(*mock_namespace_handler, Spec(NotNull()))
      .WillOnce(Return(Status::OK));

  StatusOr<ContainerSpec> statusor = container_->Spec();
  ASSERT_OK(statusor);
  ContainerSpec stats = statusor.ValueOrDie();
  EXPECT_FALSE(stats.has_cpu());
  EXPECT_FALSE(stats.has_memory());
  EXPECT_FALSE(stats.has_blockio());
  EXPECT_FALSE(stats.has_network());
  EXPECT_FALSE(stats.has_monitoring());
  EXPECT_FALSE(stats.has_filesystem());
}

TEST_F(ContainerImplTest, SpecGetResourceHandlersFails) {
  vector<MockResourceHandler *> mock_handlers =
      ExpectGetResourceHandlers(Status::CANCELLED);
  ExpectGetNamespaceHandler();

  EXPECT_EQ(Status::CANCELLED, container_->Spec().status());
}

TEST_F(ContainerImplTest, SpecResourceSpecFails) {
  vector<MockResourceHandler *> mock_handlers =
      ExpectGetResourceHandlers(Status::OK);
  ExpectGetNamespaceHandler();

  for (MockResourceHandler *mock_handler : mock_handlers) {
    EXPECT_CALL(*mock_handler, Spec(NotNull()))
        .WillRepeatedly(Return(Status::CANCELLED));
  }

  EXPECT_EQ(Status::CANCELLED, container_->Spec().status());
}

// Tests for Exec().

TEST_F(ContainerImplTest, ExecSuccess) {
  const vector<string> kCmd = {"/bin/echo", "test", "cmd"};

  ExpectExists(true);
  MockNamespaceHandler *mock_namespace_handler =
      new StrictMockNamespaceHandler(kContainerName, RESOURCE_VIRTUALHOST);
  EXPECT_CALL(*mock_namespace_handler_factory_,
              GetNamespaceHandler(kContainerName))
      .WillOnce(Return(mock_namespace_handler));
  EXPECT_CALL(*mock_namespace_handler, Exec(kCmd))
      .WillOnce(Return(Status::OK));
  ExpectEnterInto(0, Status::OK);

  // We expect INTERNAL since Exec does not typically return on success.
  EXPECT_ERROR_CODE(::util::error::INTERNAL, container_->Exec(kCmd));
}

TEST_F(ContainerImplTest, ExecContainerNotFound) {
  const vector<string> kCmd = {"/bin/echo", "test", "cmd"};

  ExpectExists(false);

  EXPECT_ERROR_CODE(::util::error::NOT_FOUND, container_->Exec(kCmd));
}

TEST_F(ContainerImplTest, ExecEnterFails) {
  const vector<string> kCmd = {"/bin/echo", "test", "cmd"};

  ExpectExists(true);
  ExpectEnterInto(0, Status::CANCELLED);

  EXPECT_EQ(Status::CANCELLED, container_->Exec(kCmd));
}

TEST_F(ContainerImplTest, ExecExecvpFails) {
  const vector<string> kCmd = {"/bin/echo", "test", "cmd"};

  ExpectExists(true);
  MockNamespaceHandler *mock_namespace_handler =
      new StrictMockNamespaceHandler(kContainerName, RESOURCE_VIRTUALHOST);
  EXPECT_CALL(*mock_namespace_handler_factory_,
              GetNamespaceHandler(kContainerName))
      .WillOnce(Return(mock_namespace_handler));
  EXPECT_CALL(*mock_namespace_handler, Exec(kCmd))
      .WillOnce(Return(Status::CANCELLED));
  ExpectEnterInto(0, Status::OK);

  EXPECT_ERROR_CODE(::util::error::CANCELLED, container_->Exec(kCmd));
}

TEST_F(ContainerImplTest, Pause_Success) {
  EXPECT_CALL(*mock_freezer_controller_, Freeze())
      .WillOnce(Return(Status::OK));
  EXPECT_OK(container_->Pause());
}

TEST_F(ContainerImplTest, Pause_Unknown_Failure) {
  EXPECT_CALL(*mock_freezer_controller_, Freeze())
      .WillOnce(Return(Status(UNIMPLEMENTED, "Freeze?")));
  EXPECT_ERROR_CODE(UNIMPLEMENTED, container_->Pause());
}

TEST_F(ContainerImplTest, Pause_Freezer_Unsupported_Failure) {
  EXPECT_CALL(*mock_freezer_controller_, Freeze())
      .WillOnce(Return(Status(NOT_FOUND, "Freezer cgroup file not found")));
  EXPECT_ERROR_CODE(FAILED_PRECONDITION, container_->Pause());
}

TEST_F(ContainerImplTest, Resume_Success) {
  EXPECT_CALL(*mock_freezer_controller_, Unfreeze())
      .WillOnce(Return(Status::OK));
  EXPECT_OK(container_->Resume());
}

TEST_F(ContainerImplTest, Resume_Unknown_Failure) {
  EXPECT_CALL(*mock_freezer_controller_, Unfreeze())
      .WillOnce(Return(Status(UNIMPLEMENTED, "Unfreeze?")));
  EXPECT_ERROR_CODE(UNIMPLEMENTED, container_->Resume());
}

TEST_F(ContainerImplTest, Resume_Freezer_Unsupported_Failure) {
  EXPECT_CALL(*mock_freezer_controller_, Unfreeze())
      .WillOnce(Return(Status(NOT_FOUND, "Freezer cgroup file not found")));
  EXPECT_ERROR_CODE(FAILED_PRECONDITION, container_->Resume());
}

TEST_F(ContainerImplTest, Name) {
  EXPECT_EQ(kContainerName, container_->name());
}

}  // namespace lmctfy
}  // namespace containers
