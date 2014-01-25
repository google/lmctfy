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
#include "lmctfy/resource_handler_mock.h"
#include "lmctfy/tasks_handler_mock.h"
#include "include/lmctfy.pb.h"
#include "include/lmctfy_mock.h"
#include "util/safe_types/unix_gid.h"
#include "util/safe_types/unix_uid.h"
#include "util/errors_test_util.h"
#include "util/file_lines_test_util.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "util/gtl/stl_util.h"
#include "util/process/mock_subprocess.h"
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
using ::testing::Cardinality;
using ::testing::ContainerEq;
using ::testing::Contains;
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
using ::util::Status;
using ::util::StatusOr;
using ::util::error::NOT_FOUND;

namespace containers {
namespace lmctfy {

typedef ::system_api::KernelAPIMock MockKernelApi;

class MockContainerApiImpl : public ContainerApiImpl {
 public:
  MockContainerApiImpl(MockTasksHandlerFactory *mock_tasks_handler_factory,
                   const CgroupFactory *cgroup_factory,
                   const vector<ResourceHandlerFactory *> &resource_factories,
                   const MockKernelApi *mock_kernel,
                   ActiveNotifications *active_notifications,
                   EventFdNotifications *eventfd_notifications)
      : ContainerApiImpl(mock_tasks_handler_factory, cgroup_factory,
                     resource_factories, mock_kernel, active_notifications,
                     eventfd_notifications) {}

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
  void SetUp() {
    mock_handler_factory1_ = new NiceMockResourceHandlerFactory(RESOURCE_CPU);
    mock_handler_factory2_ = new NiceMockResourceHandlerFactory(
        RESOURCE_MEMORY);
    mock_handler_factory3_ = new NiceMockResourceHandlerFactory(
        RESOURCE_FILESYSTEM);
    vector<ResourceHandlerFactory *> resource_factories = {
      mock_handler_factory1_, mock_handler_factory2_, mock_handler_factory3_
    };

    active_notifications_ = new ActiveNotifications();
    mock_eventfd_notifications_ = MockEventFdNotifications::NewStrict();
    mock_tasks_handler_factory_ = new StrictMockTasksHandlerFactory();
    mock_cgroup_factory_ = new StrictMockCgroupFactory();
    lmctfy_.reset(
        new ContainerApiImpl(mock_tasks_handler_factory_, mock_cgroup_factory_,
                         resource_factories, &mock_kernel_.Mock(),
                         active_notifications_, mock_eventfd_notifications_));
  }

  // Expect the machine to have a set of mounts. If cgroups_mounted is true, it
  // expects the supported hierarchies to be mounted on the machine.
  void ExpectMachineMounts(bool cgroups_mounted) {
    if (cgroups_mounted) {
      mock_file_lines_.ExpectFileLines(
          "/proc/mounts",
          {"none /dev/cgroup/cpu cgroup rw,relatime,cpuacct,cpu 0 0",
           "none /dev/cgroup/cpuset cgroup rw,relatime,cpuset 0 0",
           "rootfs / rootfs rw 0 0",
           "none /dev/cgroup/memory cgroup rw,relatime,memory 0 0",
           "none /dev/cgroup/perf_event cgroup rw,relatime,perf_event 0 0",
           "sysfs /sys sysfs rw,nosuid,nodev,noexec,relatime 0 0",
           "proc /proc proc rw,nosuid,nodev,noexec,relatime 0 0",
           "none /dev/cgroup/job cgroup rw,relatime,job 0 0", });
    } else {
      mock_file_lines_.ExpectFileLines(
          "/proc/mounts",
          {"rootfs / rootfs rw 0 0",
           "sysfs /sys sysfs rw,nosuid,nodev,noexec,relatime 0 0",
           "proc /proc proc rw,nosuid,nodev,noexec,relatime 0 0", });
    }
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
  ActiveNotifications *active_notifications_;
  MockEventFdNotifications *mock_eventfd_notifications_;

  FileLinesTestUtil mock_file_lines_;
  MockKernelApiOverride mock_kernel_;
};

// Tests for ContainerApiImpl::NewContainerApiImpl()

TEST_F(ContainerApiImplTest, NewContainerApiImpl) {
  MockCgroupFactory *mock_cgroup_factory = new StrictMockCgroupFactory();

  EXPECT_CALL(*mock_cgroup_factory, OwnsCgroup(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cgroup_factory, IsMounted(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(mock_kernel_.Mock(), EpollCreate(_)).WillRepeatedly(Return(0));

  StatusOr<ContainerApi *> statusor =
      ContainerApiImpl::NewContainerApiImpl(mock_cgroup_factory, &mock_kernel_.Mock());
  ASSERT_OK(statusor);
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  delete statusor.ValueOrDie();
}

TEST_F(ContainerApiImplTest, NewContainerApiImplNoJobHierarchy) {
  MockCgroupFactory *mock_cgroup_factory = new StrictMockCgroupFactory();

  EXPECT_CALL(*mock_cgroup_factory, OwnsCgroup(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cgroup_factory, IsMounted(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cgroup_factory, IsMounted(CGROUP_JOB))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(mock_kernel_.Mock(), EpollCreate(_)).WillRepeatedly(Return(0));

  StatusOr<ContainerApi *> statusor =
      ContainerApiImpl::NewContainerApiImpl(mock_cgroup_factory, &mock_kernel_.Mock());
  ASSERT_OK(statusor);
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  delete statusor.ValueOrDie();
}

// Tests for ContainerApiImpl::InitMachineImpl()

// Expect the specified mount to be expressed in data when it is cast to a
// string.
void ExpectMemoryMount(const string &source, const string &target,
                       const string &filesystemtype, unsigned int mountflags,
                       const void *data) {
  EXPECT_THAT("memory", StrEq(static_cast<const char *>(data)));
}
void ExpectCpuAndCpuacctMount(const string &source, const string &target,
                       const string &filesystemtype, unsigned int mountflags,
                       const void *data) {
  string actual = static_cast<const char *>(data);
  EXPECT_TRUE(actual == "cpu,cpuacct" || actual == "cpuacct,cpu");
}
void ExpectCpusetMount(const string &source, const string &target,
                       const string &filesystemtype, unsigned int mountflags,
                       const void *data) {
  EXPECT_THAT("cpuset", StrEq(static_cast<const char *>(data)));
}
void ExpectJobMount(const string &source, const string &target,
                       const string &filesystemtype, unsigned int mountflags,
                       const void *data) {
  EXPECT_THAT("job", StrEq(static_cast<const char *>(data)));
}
void ExpectPerfEventMount(const string &source, const string &target,
                       const string &filesystemtype, unsigned int mountflags,
                       const void *data) {
  EXPECT_THAT("perf_event", StrEq(static_cast<const char *>(data)));
}

TEST_F(ContainerApiImplTest, InitMachineImplSuccess) {
  ExpectMachineMounts(false);

  CgroupMount mount1;
  CgroupMount mount2;
  CgroupMount mount3;
  CgroupMount mount4;
  CgroupMount mount5;
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

  InitSpec spec;
  spec.add_cgroup_mount()->CopyFrom(mount1);
  spec.add_cgroup_mount()->CopyFrom(mount2);
  spec.add_cgroup_mount()->CopyFrom(mount3);
  spec.add_cgroup_mount()->CopyFrom(mount4);
  spec.add_cgroup_mount()->CopyFrom(mount5);

  EXPECT_CALL(mock_kernel_.Mock(), Access(_, _))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(mock_kernel_.Mock(), SafeWriteResFile(_, _, _, _))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(mock_kernel_.Mock(), EpollCreate(_))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(mock_kernel_.Mock(), MkDirRecursive("/dev/cgroup/memory"))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(mock_kernel_.Mock(), MkDirRecursive("/dev/cgroup/cpu"))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(mock_kernel_.Mock(), MkDirRecursive("/dev/cgroup/cpuset"))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(mock_kernel_.Mock(), MkDirRecursive("/dev/cgroup/job"))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(mock_kernel_.Mock(), MkDirRecursive("/dev/cgroup/perf_event"))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(mock_kernel_.Mock(),
              Mount("cgroup", "/dev/cgroup/memory", "cgroup", 0, _))
      .WillOnce(DoAll(Invoke(&ExpectMemoryMount), Return(0)));
  EXPECT_CALL(mock_kernel_.Mock(),
              Mount("cgroup", "/dev/cgroup/cpu", "cgroup", 0, _))
      .WillOnce(DoAll(Invoke(&ExpectCpuAndCpuacctMount), Return(0)));
  EXPECT_CALL(mock_kernel_.Mock(),
              Mount("cgroup", "/dev/cgroup/cpuset", "cgroup", 0, _))
      .WillOnce(DoAll(Invoke(&ExpectCpusetMount), Return(0)));
  EXPECT_CALL(mock_kernel_.Mock(),
              Mount("cgroup", "/dev/cgroup/job", "cgroup", 0, _))
      .WillOnce(DoAll(Invoke(&ExpectJobMount), Return(0)));
  EXPECT_CALL(mock_kernel_.Mock(),
              Mount("cgroup", "/dev/cgroup/perf_event", "cgroup", 0, _))
      .WillOnce(DoAll(Invoke(&ExpectPerfEventMount), Return(0)));

  EXPECT_OK(ContainerApiImpl::InitMachineImpl(&mock_kernel_.Mock(), spec));
}

TEST_F(ContainerApiImplTest, InitMachineImplPartiallyInitialized) {
  ExpectMachineMounts(true);

  CgroupMount mount1;
  CgroupMount mount2;
  CgroupMount mount3;
  CgroupMount mount4;
  CgroupMount mount5;
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

  InitSpec spec;
  spec.add_cgroup_mount()->CopyFrom(mount1);
  spec.add_cgroup_mount()->CopyFrom(mount2);
  spec.add_cgroup_mount()->CopyFrom(mount3);
  spec.add_cgroup_mount()->CopyFrom(mount4);
  spec.add_cgroup_mount()->CopyFrom(mount5);

  EXPECT_CALL(mock_kernel_.Mock(), Access(_, _))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(mock_kernel_.Mock(), SafeWriteResFile(_, _, _, _))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(mock_kernel_.Mock(), EpollCreate(_))
      .WillRepeatedly(Return(0));

  // All cgroups are accessible.
  vector<string> kPaths = {
    "/dev/cgroup/cpu",
    "/dev/cgroup/cpuset",
    "/dev/cgroup/job",
    "/dev/cgroup/memory",
    "/dev/cgroup/perf_event",
  };
  for (const string &hierarchy : kPaths) {
    EXPECT_CALL(mock_kernel_.Mock(), Access(hierarchy, R_OK))
        .WillRepeatedly(Return(0));
  }

  EXPECT_OK(ContainerApiImpl::InitMachineImpl(&mock_kernel_.Mock(), spec));
}

TEST_F(ContainerApiImplTest, InitMachineImplMountFails) {
  ExpectMachineMounts(false);

  CgroupMount mount1;
  mount1.set_mount_path("/dev/cgroup/memory");
  mount1.add_hierarchy(CGROUP_MEMORY);

  InitSpec spec;
  spec.add_cgroup_mount()->CopyFrom(mount1);

  EXPECT_CALL(mock_kernel_.Mock(), MkDirRecursive("/dev/cgroup/memory"))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(mock_kernel_.Mock(),
              Mount("cgroup", "/dev/cgroup/memory", "cgroup", 0, _))
      .WillOnce(Return(1));

  EXPECT_NOT_OK(ContainerApiImpl::InitMachineImpl(&mock_kernel_.Mock(), spec));
}

// Tests for Get()

TEST_F(ContainerApiImplTest, GetSuccess) {
  const string kName = "/test";

  EXPECT_CALL(*mock_tasks_handler_factory_, Exists(kName))
      .WillRepeatedly(Return(true));

  // Get the tasks handler
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

  EXPECT_CALL(*mock_tasks_handler_factory_, Get(kName))
      .WillRepeatedly(Return(StatusOr<TasksHandler *>(Status::CANCELLED)));

  StatusOr<Container *> status = lmctfy_->Get(kName);
  EXPECT_EQ(::util::error::CANCELLED, status.status().error_code());
}

TEST_F(ContainerApiImplTest, GetBadContainerName) {
  StatusOr<Container *> status = lmctfy_->Get("*");
  EXPECT_EQ(::util::error::INVALID_ARGUMENT, status.status().error_code());
}

// Tests for Create()

TEST_F(ContainerApiImplTest, CreateSuccess) {
  const string kName = "/test";

  ContainerSpec spec;
  spec.mutable_cpu();
  spec.mutable_memory();
  spec.mutable_filesystem();

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
  spec.mutable_diskio();
  spec.mutable_network();
  spec.mutable_monitoring();
  spec.mutable_filesystem();

  MockResourceHandlerFactory *cpu_handler =
      new StrictMockResourceHandlerFactory(RESOURCE_CPU);
  MockResourceHandlerFactory *memory_handler =
      new StrictMockResourceHandlerFactory(RESOURCE_MEMORY);
  MockResourceHandlerFactory *diskio_handler =
      new StrictMockResourceHandlerFactory(RESOURCE_DISKIO);
  MockResourceHandlerFactory *network_handler =
      new StrictMockResourceHandlerFactory(RESOURCE_NETWORK);
  MockResourceHandlerFactory *monitoring_handler =
      new StrictMockResourceHandlerFactory(RESOURCE_MONITORING);
  MockResourceHandlerFactory *filesystem_handler =
      new StrictMockResourceHandlerFactory(RESOURCE_FILESYSTEM);

  const vector<ResourceHandlerFactory *> resource_factories = {
    cpu_handler, memory_handler, diskio_handler, network_handler,
    monitoring_handler, filesystem_handler
  };

  MockTasksHandlerFactory *mock_tasks_handler_factory =
      new StrictMockTasksHandlerFactory();
  MockCgroupFactory *mock_cgroup_factory = new StrictMockCgroupFactory();
  lmctfy_.reset(new ContainerApiImpl(
      mock_tasks_handler_factory, mock_cgroup_factory, resource_factories,
      new StrictMock<MockKernelApi>(), new ActiveNotifications(),
      MockEventFdNotifications::NewStrict()));

  EXPECT_CALL(*mock_tasks_handler_factory, Exists(kName))
      .WillRepeatedly(Return(false));

  EXPECT_CALL(*cpu_handler, Create(kName, _))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          new StrictMockResourceHandler(kName, RESOURCE_CPU))));
  EXPECT_CALL(*memory_handler, Create(kName, _))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          new StrictMockResourceHandler(kName, RESOURCE_MEMORY))));
  EXPECT_CALL(*diskio_handler, Create(kName, _))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          new StrictMockResourceHandler(kName, RESOURCE_DISKIO))));
  EXPECT_CALL(*network_handler, Create(kName, _))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          new StrictMockResourceHandler(kName, RESOURCE_NETWORK))));
  EXPECT_CALL(*monitoring_handler, Create(kName, _))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          new StrictMockResourceHandler(kName, RESOURCE_MONITORING))));
  EXPECT_CALL(*filesystem_handler, Create(kName, _))
      .WillRepeatedly(Return(StatusOr<ResourceHandler *>(
          new StrictMockResourceHandler(kName, RESOURCE_FILESYSTEM))));
  EXPECT_CALL(*mock_tasks_handler_factory,
              Create(kName, EqualsInitializedProto(spec)))
      .WillRepeatedly(
           Return(StatusOr<TasksHandler *>(new StrictMockTasksHandler(kName))));

  StatusOr<Container *> status = lmctfy_->Create(kName, spec);
  ASSERT_TRUE(status.ok());
  EXPECT_NE(static_cast<Container *>(NULL), status.ValueOrDie());
  delete status.ValueOrDie();
}

TEST_F(ContainerApiImplTest, CreateOnlySpecCpuSuccess) {
  const string kParentName = "/";
  const string kName = "/test";

  ContainerSpec spec;
  spec.mutable_cpu();

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

TEST_F(ContainerApiImplTest, CreateResourceCreationFails) {
  const string kParentName = "/";
  const string kName = "/test";

  ContainerSpec spec;
  spec.mutable_cpu();
  spec.mutable_filesystem();

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
  EXPECT_CALL(*mock_tasks_handler_factory_,
              Create(kName, EqualsInitializedProto(spec)))
      .WillRepeatedly(
           Return(StatusOr<TasksHandler *>(new StrictMockTasksHandler(kName))));

  // The partially created resources will be destroyed
  EXPECT_CALL(*cpu_handler, Destroy())
      .WillRepeatedly(Return(Status::OK));

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

TEST_F(ContainerApiImplTest, CreateWithDelegatedUser) {
  const string kName = "/test";
  const UnixUid kUid(42);
  const UnixGid kGid(UnixGidValue::Invalid());

  ContainerSpec spec;
  spec.set_owner(kUid.value());
  spec.mutable_cpu();

  MockResourceHandler *mock_cpu_handler =
      new StrictMockResourceHandler(kName, RESOURCE_CPU);
  MockTasksHandler *mock_tasks_handler = new StrictMockTasksHandler(kName);

  EXPECT_CALL(*mock_tasks_handler_factory_, Exists(kName))
      .WillRepeatedly(Return(false));

  EXPECT_CALL(*mock_handler_factory1_, Create(kName, _))
      .WillOnce(Return(mock_cpu_handler));
  EXPECT_CALL(*mock_tasks_handler_factory_,
              Create(kName, EqualsInitializedProto(spec)))
      .WillOnce(Return(mock_tasks_handler));

  EXPECT_CALL(*mock_cpu_handler, Delegate(kUid, kGid))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_tasks_handler, Delegate(kUid, kGid))
      .WillOnce(Return(Status::OK));

  StatusOr<Container *> status = lmctfy_->Create(kName, spec);
  ASSERT_OK(status);
  delete status.ValueOrDie();
}

TEST_F(ContainerApiImplTest, CreateWithDelegatedGid) {
  const string kName = "/test";
  const UnixUid kUid(UnixUidValue::Invalid());
  const UnixGid kGid(42);

  ContainerSpec spec;
  spec.set_owner_group(kGid.value());
  spec.mutable_cpu();

  MockResourceHandler *mock_cpu_handler =
      new StrictMockResourceHandler(kName, RESOURCE_CPU);
  MockTasksHandler *mock_tasks_handler = new StrictMockTasksHandler(kName);

  EXPECT_CALL(*mock_tasks_handler_factory_, Exists(kName))
      .WillRepeatedly(Return(false));

  EXPECT_CALL(*mock_handler_factory1_, Create(kName, _))
      .WillOnce(Return(mock_cpu_handler));
  EXPECT_CALL(*mock_tasks_handler_factory_,
              Create(kName, EqualsInitializedProto(spec)))
      .WillOnce(Return(mock_tasks_handler));

  EXPECT_CALL(*mock_cpu_handler, Delegate(kUid, kGid))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_tasks_handler, Delegate(kUid, kGid))
      .WillOnce(Return(Status::OK));

  StatusOr<Container *> status = lmctfy_->Create(kName, spec);
  ASSERT_OK(status);
  delete status.ValueOrDie();
}

TEST_F(ContainerApiImplTest, CreateWithDelegatedUserDelegateTasksHandlerFails) {
  const string kName = "/test";
  const UnixUid kUid(42);
  const UnixGid kGid(UnixGidValue::Invalid());

  ContainerSpec spec;
  spec.set_owner(kUid.value());
  spec.mutable_cpu();

  MockResourceHandler *mock_cpu_handler =
      new StrictMockResourceHandler(kName, RESOURCE_CPU);
  MockTasksHandler *mock_tasks_handler = new StrictMockTasksHandler(kName);

  EXPECT_CALL(*mock_tasks_handler_factory_, Exists(kName))
      .WillRepeatedly(Return(false));

  EXPECT_CALL(*mock_handler_factory1_, Create(kName, _))
      .WillOnce(Return(mock_cpu_handler));
  EXPECT_CALL(*mock_tasks_handler_factory_,
              Create(kName, EqualsInitializedProto(spec)))
      .WillOnce(Return(mock_tasks_handler));

  EXPECT_CALL(*mock_cpu_handler, Delegate(kUid, kGid))
      .WillRepeatedly(Return(Status::CANCELLED));
  EXPECT_CALL(*mock_tasks_handler, Delegate(kUid, kGid))
      .WillRepeatedly(Return(Status::OK));

  EXPECT_ERROR_CODE(::util::error::CANCELLED, lmctfy_->Create(kName, spec));
}

TEST_F(ContainerApiImplTest, CreateWithDelegatedUserDelegateResourceHandlerFails) {
  const string kName = "/test";
  const UnixUid kUid(42);
  const UnixGid kGid(UnixGidValue::Invalid());

  ContainerSpec spec;
  spec.set_owner(kUid.value());
  spec.mutable_cpu();

  MockResourceHandler *mock_cpu_handler =
      new StrictMockResourceHandler(kName, RESOURCE_CPU);
  MockTasksHandler *mock_tasks_handler = new StrictMockTasksHandler(kName);

  EXPECT_CALL(*mock_tasks_handler_factory_, Exists(kName))
      .WillRepeatedly(Return(false));

  EXPECT_CALL(*mock_handler_factory1_, Create(kName, _))
      .WillOnce(Return(mock_cpu_handler));
  EXPECT_CALL(*mock_tasks_handler_factory_,
              Create(kName, EqualsInitializedProto(spec)))
      .WillOnce(Return(mock_tasks_handler));

  EXPECT_CALL(*mock_cpu_handler, Delegate(kUid, kGid))
      .WillRepeatedly(Return(Status::OK));
  EXPECT_CALL(*mock_tasks_handler, Delegate(kUid, kGid))
      .WillRepeatedly(Return(Status::CANCELLED));

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
    vector<ResourceHandlerFactory *> resource_factories;

    // Add dummy Global Resource Handler
    resource_factories.push_back(
        new StrictMockResourceHandlerFactory(RESOURCE_CPU));

    mock_tasks_handler_factory_ = new StrictMockTasksHandlerFactory();
    mock_cgroup_factory_ = new StrictMockCgroupFactory();
    active_notifications_ = new ActiveNotifications();
    mock_eventfd_notifications_ = MockEventFdNotifications::NewStrict();
    mock_lmctfy_.reset(new StrictMock<MockContainerApiImpl>(
        mock_tasks_handler_factory_, mock_cgroup_factory_, resource_factories,
        &mock_kernel_.Mock(), active_notifications_,
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

// Tests for InitMachine()

TEST_F(ContainerApiImplTest, InitMachineSuccess) {
  InitSpec spec;
  spec.mutable_memory();

  EXPECT_CALL(*mock_handler_factory1_,
              InitMachine(EqualsInitializedProto(spec)))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_handler_factory2_,
              InitMachine(EqualsInitializedProto(spec)))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_handler_factory3_,
              InitMachine(EqualsInitializedProto(spec)))
      .WillOnce(Return(Status::OK));

  EXPECT_OK(lmctfy_->InitMachine(spec));
}

TEST_F(ContainerApiImplTest, InitMachineResourceHandlerInitFails) {
  InitSpec spec;
  spec.mutable_memory();

  EXPECT_CALL(*mock_handler_factory1_,
              InitMachine(EqualsInitializedProto(spec)))
      .WillRepeatedly(Return(Status::OK));
  EXPECT_CALL(*mock_handler_factory2_,
              InitMachine(EqualsInitializedProto(spec)))
      .WillRepeatedly(Return(Status::CANCELLED));
  EXPECT_CALL(*mock_handler_factory3_,
              InitMachine(EqualsInitializedProto(spec)))
      .WillRepeatedly(Return(Status::OK));

  EXPECT_EQ(Status::CANCELLED, lmctfy_->InitMachine(spec));
}

// Simply returns the specified subprocess.
SubProcess *IdentitySubProcessFactory(MockSubProcess *subprocess) {
  return subprocess;
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

    mock_lmctfy_.reset(new StrictMock<MockContainerApiImpl>(
        new StrictMockTasksHandlerFactory(), new StrictMockCgroupFactory(),
        vector<ResourceHandlerFactory *>(), &mock_kernel_.Mock(),
        new ActiveNotifications(), MockEventFdNotifications::NewStrict()));
    mock_subprocess_ = new StrictMock<MockSubProcess>();
    mock_subprocess_factory_.reset(
        NewPermanentCallback(&IdentitySubProcessFactory, mock_subprocess_));

    ResourceFactoryMap factory_map;
    factory_map.insert(make_pair(RESOURCE_CPU, mock_resource_factory1_.get()));
    factory_map.insert(make_pair(RESOURCE_MEMORY,
                                 mock_resource_factory2_.get()));
    factory_map.insert(make_pair(RESOURCE_FILESYSTEM,
                                 mock_resource_factory3_.get()));

    container_.reset(new ContainerImpl(
        container_name,
        mock_tasks_handler_,
        factory_map,
        mock_lmctfy_.get(),
        &mock_kernel_.Mock(),
        mock_subprocess_factory_.get(),
        &active_notifications_));

    ExpectExists(true);
  }

  // Expect the specified container to have the subcontainers with the specified
  // names. This creates mocks for those subcontainers and adds them to
  // container_map_. By default we expect the subcontainers to have no
  // containers.
  void ExpectSubcontainers(const string &container_name,
                           const vector<string> &subcontainer_names) {
    vector<Container *> subcontainers;

    // Create the subcontainers and expect their Get()s.
    for (const string &name : subcontainer_names) {
      MockContainer *sub = new StrictMockContainer(name);

      EXPECT_CALL(*mock_lmctfy_, Get(name))
          .WillRepeatedly(Return(StatusOr<Container *>(sub)));
      EXPECT_CALL(*sub, ListSubcontainers(Container::LIST_SELF))
          .WillRepeatedly(
              Return(StatusOr<vector<Container *>>(vector<Container *>())));

      subcontainers.push_back(sub);
      container_map_[name] = sub;
    }

    // The top level container should use the TasksHandler, else use this
    // container's mock.
    if (container_name == kContainerName) {
      EXPECT_CALL(*mock_tasks_handler_, ListSubcontainers())
          .WillRepeatedly(Return(StatusOr<vector<string>>(subcontainer_names)));
    } else {
      CHECK(container_map_.find(container_name) != container_map_.end())
          << "MockContainer has not been created yet";
      EXPECT_CALL(*(container_map_[container_name]),
                  ListSubcontainers(Container::LIST_SELF))
          .WillRepeatedly(Return(StatusOr<vector<Container *>>(subcontainers)));
    }
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
    ExpectSubcontainers(container_->name(),
                        {kSub1, kSub2});

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
      EXPECT_CALL(*mock_tasks_handler_, ListProcesses())
          .WillRepeatedly(Return(vector<pid_t>()));
      EXPECT_CALL(*mock_tasks_handler_, ListThreads())
          .WillRepeatedly(Return(vector<pid_t>()));
    } else {
      EXPECT_CALL(*mock_tasks_handler_, ListProcesses())
          .WillRepeatedly(Return(status));
      EXPECT_CALL(*mock_tasks_handler_, ListThreads())
          .WillRepeatedly(Return(status));
    }

    EXPECT_CALL(mock_kernel_.Mock(), Usleep(
        FLAGS_lmctfy_ms_delay_between_kills * 1000))
        .WillRepeatedly(Return(0));
  }

  // Expect Enter() to be called on the specified TID and return with the
  // specified status.
  void ExpectEnter(pid_t pid, Status status) {
    vector<pid_t> tids = {pid};

    EXPECT_CALL(*mock_tasks_handler_, TrackTasks(tids))
        .WillRepeatedly(Return(Status::OK));

    // Enter() takes ownership of the return of GetResourceHandlers().
    vector<MockResourceHandler *> handlers = ExpectGetResourceHandlers(
        Status::OK);
    for (MockResourceHandler *handler : handlers) {
      EXPECT_CALL(*handler, Enter(tids))
          .WillRepeatedly(Return(status));
    }
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
  MockSubProcess *mock_subprocess_;
  unique_ptr<SubProcessFactory> mock_subprocess_factory_;
  MockKernelApiOverride mock_kernel_;
};

// Tests for ListSubcontainers().

TEST_F(ContainerImplTest, ListSubcontainersNoContainer) {
  // Call for self and recursive
  for (const auto &policy : list_policies_) {
    ExpectExists(false);

    EXPECT_ERROR_CODE(NOT_FOUND, container_->ListSubcontainers(policy));
  }
}

TEST_F(ContainerImplTest, ListSubcontainersNoSubcontainers) {
  // Call for self and recursive
  for (const auto &policy : list_policies_) {
    ExpectSubcontainers(container_->name(), vector<string>());

    StatusOr<vector<Container *>> statusor = container_->ListSubcontainers(
        policy);
    ASSERT_TRUE(statusor.ok());
    EXPECT_EQ(0, statusor.ValueOrDie().size());
  }
}

TEST_F(ContainerImplTest, ListSubcontainersOneLevel) {
  // Call for self and recursive
  for (const auto &policy : list_policies_) {
    ExpectSubcontainers(container_->name(), {JoinPath(kContainerName, "sub2"),
                                             JoinPath(kContainerName, "sub1")});

    StatusOr<vector<Container *>> statusor = container_->ListSubcontainers(
        policy);
    ASSERT_TRUE(statusor.ok());
    vector<Container *> subcontainers = statusor.ValueOrDie();
    EXPECT_EQ(2, subcontainers.size());
    ExpectContainers(subcontainers);
    STLDeleteElements(&subcontainers);
  }
}

TEST_F(ContainerImplTest, ListSubcontainersSelfListFails) {
  EXPECT_CALL(*mock_tasks_handler_, ListSubcontainers())
      .WillRepeatedly(Return(StatusOr<vector<string>>(Status::CANCELLED)));

  EXPECT_EQ(Status::CANCELLED, container_->ListSubcontainers(
      Container::LIST_SELF).status());
}

TEST_F(ContainerImplTest, ListSubcontainersSelfContainerApiGetFails) {
  const string kSub = JoinPath(kContainerName, "sub1");

  ExpectSubcontainers(container_->name(), {kSub});

  // Getting one of the containers fails.
  EXPECT_CALL(*mock_lmctfy_, Get(kSub))
      .WillRepeatedly(Return(StatusOr<Container *>(Status::CANCELLED)));

  EXPECT_EQ(Status::CANCELLED, container_->ListSubcontainers(
      Container::LIST_SELF).status());

  // Since we failed the Get() it is safe to delete the mock container.
  STLDeleteValues(&container_map_);
}

TEST_F(ContainerImplTest, ListSubcontainersRecursiveWithManyLayers) {
  const string kSub1 = "sub2";
  const string kSub2 = "sub1";
  const string kSub3 = JoinPath(kSub2, "ssub2");
  const string kSub4 = JoinPath(kSub2, "ssub1");
  const string kSub5 = JoinPath(kSub2, "ssub1/sssub1");

  ExpectSubcontainers(container_->name(), {kSub1, kSub2});
  ExpectSubcontainers(kSub2, {kSub3, kSub4});
  ExpectSubcontainers(kSub4, {kSub5});

  StatusOr<vector<Container *>> statusor = container_->ListSubcontainers(
      Container::LIST_RECURSIVE);
  ASSERT_TRUE(statusor.ok());
  vector<Container *> subcontainers = statusor.ValueOrDie();
  EXPECT_EQ(5, subcontainers.size());
  ExpectContainers(subcontainers);
  STLDeleteElements(&subcontainers);
}

TEST_F(ContainerImplTest, ListSubcontainersRecursiveListSubcontainersFails) {
  const string kSub1 = "/test/sub1";
  const string kSub2 = "/test/sub2";
  const string kSub3 = JoinPath(kSub1, "ssub1");
  const string kSub4 = JoinPath(kSub2, "ssub1");

  ExpectSubcontainers(container_->name(), {kSub1, kSub2});
  ExpectSubcontainers(kSub1, {kSub3});
  ExpectSubcontainers(kSub2, {kSub4});

  // Set Get() of one subcontainer to fail
  EXPECT_CALL(*(container_map_[kSub4]), ListSubcontainers(Container::LIST_SELF))
      .WillRepeatedly(Return(StatusOr<vector<Container *>>(Status::CANCELLED)));

  EXPECT_EQ(Status::CANCELLED, container_->ListSubcontainers(
      Container::LIST_RECURSIVE).status());
}

// Tests for ListThreads().

TEST_F(ContainerImplTest, ListThreadsNoContainer) {
  // Call for self and recursive
  for (const auto &policy : list_policies_) {
    ExpectExists(false);

    EXPECT_ERROR_CODE(NOT_FOUND, container_->ListThreads(policy));
  }
}

TEST_F(ContainerImplTest, ListThreadsSelfSuccess) {
  const vector<pid_t> kPids = {1, 2, 3, 4};

  EXPECT_CALL(*mock_tasks_handler_, ListThreads())
      .WillRepeatedly(Return(kPids));

  StatusOr<vector<pid_t>> statusor = container_->ListThreads(
      Container::LIST_SELF);
  ASSERT_TRUE(statusor.ok());
  EXPECT_EQ(kPids, statusor.ValueOrDie());
}

TEST_F(ContainerImplTest, ListThreadsSelfEmpty) {
  EXPECT_CALL(*mock_tasks_handler_, ListThreads())
      .WillRepeatedly(Return(vector<pid_t>()));

  StatusOr<vector<pid_t>> statusor = container_->ListThreads(
      Container::LIST_SELF);
  ASSERT_TRUE(statusor.ok());
  EXPECT_TRUE(statusor.ValueOrDie().empty());
}

TEST_F(ContainerImplTest, ListThreadsSelfError) {
  EXPECT_CALL(*mock_tasks_handler_, ListThreads())
      .WillRepeatedly(Return(Status::CANCELLED));

  StatusOr<vector<pid_t>> statusor = container_->ListThreads(
      Container::LIST_SELF);
  ASSERT_FALSE(statusor.ok());
  EXPECT_EQ(Status::CANCELLED, statusor.status());
}

TEST_F(ContainerImplTest, ListThreadsRecursiveSuccess) {
  // Expect the PIDs for self.
  const vector<pid_t> kPids = {1, 2};
  EXPECT_CALL(*mock_tasks_handler_, ListThreads())
      .WillRepeatedly(Return(kPids));

  // Expect 2 subcontainers each with PIDs.
  vector<MockContainer *> subcontainers =
      ExpectSubcontainersListPids(false, {3, 4},
                                  {5, 6});
  ElementDeleter d(&subcontainers);

  StatusOr<vector<pid_t>> statusor = container_->ListThreads(
      Container::LIST_RECURSIVE);
  ASSERT_TRUE(statusor.ok());
  const vector<pid_t> kExpected = {1, 2, 3, 4, 5, 6};
  EXPECT_EQ(kExpected, statusor.ValueOrDie());
}

TEST_F(ContainerImplTest, ListThreadsRecursiveWithDuplicates) {
  // Expect the PIDs for self.
  const vector<pid_t> kPids = {1, 2};
  EXPECT_CALL(*mock_tasks_handler_, ListThreads())
      .WillRepeatedly(Return(kPids));

  // Expect 2 subcontainers each with PIDs.
  vector<MockContainer *> subcontainers =
      ExpectSubcontainersListPids(false, {1, 2, 3, 4},
                                  {3, 4, 5, 6});
  ElementDeleter d(&subcontainers);

  StatusOr<vector<pid_t>> statusor = container_->ListThreads(
      Container::LIST_RECURSIVE);
  ASSERT_TRUE(statusor.ok());
  const vector<pid_t> kExpected = {1, 2, 3, 4, 5, 6};
  EXPECT_EQ(kExpected, statusor.ValueOrDie());
}

TEST_F(ContainerImplTest, ListThreadsRecursiveNoSubcontainers) {
  // Expect the PIDs for self.
  const vector<pid_t> kPids = {1, 2};
  EXPECT_CALL(*mock_tasks_handler_, ListThreads())
      .WillRepeatedly(Return(kPids));

  // Expect no subcontainers.
  ExpectSubcontainers(container_->name(), vector<string>());

  StatusOr<vector<pid_t>> statusor = container_->ListThreads(
      Container::LIST_RECURSIVE);
  ASSERT_TRUE(statusor.ok());
  EXPECT_EQ(kPids, statusor.ValueOrDie());
}

TEST_F(ContainerImplTest, ListThreadsRecursiveListSubcontainersFails) {
  // Expect the PIDs for self.
  const vector<pid_t> kPids = {1, 2};
  EXPECT_CALL(*mock_tasks_handler_, ListThreads())
      .WillRepeatedly(Return(kPids));

  // Subcontainers should fail.
  EXPECT_CALL(*mock_tasks_handler_, ListSubcontainers())
      .WillRepeatedly(Return(Status::CANCELLED));

  StatusOr<vector<pid_t>> statusor = container_->ListThreads(
      Container::LIST_RECURSIVE);
  ASSERT_FALSE(statusor.ok());
  EXPECT_EQ(Status::CANCELLED, statusor.status());
}

TEST_F(ContainerImplTest, ListThreadsRecursiveListThreadsFails) {
  // Expect the PIDs for self.
  const vector<pid_t> kPids = {1, 2};
  EXPECT_CALL(*mock_tasks_handler_, ListThreads())
      .WillRepeatedly(Return(kPids));

  // Expect 2 subcontainers each with PIDs.
  vector<MockContainer *> subcontainers =
      ExpectSubcontainersListPids(false, {3, 4},
                                  {5, 6});
  ElementDeleter d(&subcontainers);

  // Set the last subcontainer to fails a ListThreads.
  EXPECT_CALL(*subcontainers.back(), ListThreads(Container::LIST_SELF))
      .WillRepeatedly(Return(Status::CANCELLED));

  StatusOr<vector<pid_t>> statusor = container_->ListThreads(
      Container::LIST_RECURSIVE);
  ASSERT_FALSE(statusor.ok());
  EXPECT_EQ(Status::CANCELLED, statusor.status());
}

// Tests for ListProcesses().

TEST_F(ContainerImplTest, ListProcessesNoContainer) {
  // Call for self and recursive
  for (const auto &policy : list_policies_) {
    ExpectExists(false);

    EXPECT_ERROR_CODE(NOT_FOUND, container_->ListProcesses(policy));
  }
}

TEST_F(ContainerImplTest, ListProcessesSelfSuccess) {
  const vector<pid_t> kPids = {1, 2, 3, 4};

  EXPECT_CALL(*mock_tasks_handler_, ListProcesses())
      .WillRepeatedly(Return(kPids));

  StatusOr<vector<pid_t>> statusor = container_->ListProcesses(
      Container::LIST_SELF);
  ASSERT_TRUE(statusor.ok());
  EXPECT_EQ(kPids, statusor.ValueOrDie());
}

TEST_F(ContainerImplTest, ListProcessesSelfEmpty) {
  EXPECT_CALL(*mock_tasks_handler_, ListProcesses())
      .WillRepeatedly(Return(vector<pid_t>()));

  StatusOr<vector<pid_t>> statusor = container_->ListProcesses(
      Container::LIST_SELF);
  ASSERT_TRUE(statusor.ok());
  EXPECT_TRUE(statusor.ValueOrDie().empty());
}

TEST_F(ContainerImplTest, ListProcessesSelfError) {
  EXPECT_CALL(*mock_tasks_handler_, ListProcesses())
      .WillRepeatedly(Return(Status::CANCELLED));

  StatusOr<vector<pid_t>> statusor = container_->ListProcesses(
      Container::LIST_SELF);
  ASSERT_FALSE(statusor.ok());
  EXPECT_EQ(Status::CANCELLED, statusor.status());
}

TEST_F(ContainerImplTest, ListProcessesRecursiveSuccess) {
  // Expect the PIDs for self.
  const vector<pid_t> kPids = {1, 2};
  EXPECT_CALL(*mock_tasks_handler_, ListProcesses())
      .WillRepeatedly(Return(kPids));

  // Expect 2 subcontainers each with PIDs.
  vector<MockContainer *> subcontainers =
      ExpectSubcontainersListPids(true, {3, 4},
                                  {5, 6});
  ElementDeleter d(&subcontainers);

  StatusOr<vector<pid_t>> statusor = container_->ListProcesses(
      Container::LIST_RECURSIVE);
  ASSERT_TRUE(statusor.ok());
  const vector<pid_t> kExpected = {1, 2, 3, 4, 5, 6};
  EXPECT_EQ(kExpected, statusor.ValueOrDie());
}

TEST_F(ContainerImplTest, ListProcessesRecursiveWithDuplicates) {
  // Expect the PIDs for self.
  const vector<pid_t> kPids = {1, 2};
  EXPECT_CALL(*mock_tasks_handler_, ListProcesses())
      .WillRepeatedly(Return(kPids));

  // Expect 2 subcontainers each with PIDs.
  vector<MockContainer *> subcontainers =
      ExpectSubcontainersListPids(true, {1, 2, 3, 4},
                                  {3, 4, 5, 6});
  ElementDeleter d(&subcontainers);

  StatusOr<vector<pid_t>> statusor = container_->ListProcesses(
      Container::LIST_RECURSIVE);
  ASSERT_TRUE(statusor.ok());
  const vector<pid_t> kExpected = {1, 2, 3, 4, 5, 6};
  EXPECT_EQ(kExpected, statusor.ValueOrDie());
}

TEST_F(ContainerImplTest, ListProcessesRecursiveNoSubcontainers) {
  // Expect the PIDs for self.
  const vector<pid_t> kPids = {1, 2};
  EXPECT_CALL(*mock_tasks_handler_, ListProcesses())
      .WillRepeatedly(Return(kPids));

  // Expect no subcontainers.
  ExpectSubcontainers(container_->name(), vector<string>());

  StatusOr<vector<pid_t>> statusor = container_->ListProcesses(
      Container::LIST_RECURSIVE);
  ASSERT_TRUE(statusor.ok());
  EXPECT_EQ(kPids, statusor.ValueOrDie());
}

TEST_F(ContainerImplTest, ListProcessesRecursiveListSubcontainersFails) {
  // Expect the PIDs for self.
  const vector<pid_t> kPids = {1, 2};
  EXPECT_CALL(*mock_tasks_handler_, ListProcesses())
      .WillRepeatedly(Return(kPids));

  // Subcontainers should fail.
  EXPECT_CALL(*mock_tasks_handler_, ListSubcontainers())
      .WillRepeatedly(Return(Status::CANCELLED));

  StatusOr<vector<pid_t>> statusor = container_->ListProcesses(
      Container::LIST_RECURSIVE);
  ASSERT_FALSE(statusor.ok());
  EXPECT_EQ(Status::CANCELLED, statusor.status());
}

TEST_F(ContainerImplTest, ListProcessesRecursiveListProcessesFails) {
  // Expect the PIDs for self.
  const vector<pid_t> kPids = {1, 2};
  EXPECT_CALL(*mock_tasks_handler_, ListProcesses())
      .WillRepeatedly(Return(kPids));

  // Expect 2 subcontainers each with PIDs.
  vector<MockContainer *> subcontainers =
      ExpectSubcontainersListPids(true, {3, 4},
                                  {5, 6});
  ElementDeleter d(&subcontainers);

  // Set the last subcontainer to fails a ListThreads.
  EXPECT_CALL(*subcontainers.back(), ListProcesses(Container::LIST_SELF))
      .WillRepeatedly(Return(Status::CANCELLED));

  StatusOr<vector<pid_t>> statusor = container_->ListProcesses(
      Container::LIST_RECURSIVE);
  ASSERT_FALSE(statusor.ok());
  EXPECT_EQ(Status::CANCELLED, statusor.status());
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

  StatusOr<vector<ResourceHandler *>> statusor = CallGetResourceHandlers();
  ASSERT_TRUE(statusor.ok());

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

    StatusOr<ContainerStats> statusor = container_->Stats(type);
    ASSERT_TRUE(statusor.ok());
    ContainerStats stats = statusor.ValueOrDie();
    EXPECT_TRUE(stats.has_cpu());
    EXPECT_TRUE(stats.has_memory());
    EXPECT_FALSE(stats.has_diskio());
    EXPECT_FALSE(stats.has_network());
    EXPECT_FALSE(stats.has_monitoring());
    EXPECT_FALSE(stats.has_filesystem());
  }
}

TEST_F(ContainerImplTest, StatsEmpty) {
  for (Container::StatsType type : stats_types_) {
    vector<MockResourceHandler *> mock_handlers = ExpectGetResourceHandlers(
        Status::OK);

    for (MockResourceHandler *mock_handler : mock_handlers) {
      EXPECT_CALL(*mock_handler, Stats(type, NotNull()))
          .WillRepeatedly(Return(Status::OK));
    }

    StatusOr<ContainerStats> statusor = container_->Stats(type);
    ASSERT_TRUE(statusor.ok());
    ContainerStats stats = statusor.ValueOrDie();
    EXPECT_FALSE(stats.has_cpu());
    EXPECT_FALSE(stats.has_memory());
    EXPECT_FALSE(stats.has_diskio());
    EXPECT_FALSE(stats.has_network());
    EXPECT_FALSE(stats.has_monitoring());
    EXPECT_FALSE(stats.has_filesystem());
  }
}

TEST_F(ContainerImplTest, StatsGetResourceHandlersFails) {
  for (Container::StatsType type : stats_types_) {
    vector<MockResourceHandler *> mock_handlers = ExpectGetResourceHandlers(
        Status::CANCELLED);

    EXPECT_EQ(Status::CANCELLED, container_->Stats(type).status());
  }
}

TEST_F(ContainerImplTest, StatsResourceStatsFails) {
  for (Container::StatsType type : stats_types_) {
    vector<MockResourceHandler *> mock_handlers = ExpectGetResourceHandlers(
        Status::OK);

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
  EXPECT_CALL(*mock_tasks_handler_, ListProcesses())
      .WillRepeatedly(Return(vector<pid_t>()));
  EXPECT_CALL(*mock_tasks_handler_, ListThreads())
      .WillRepeatedly(Return(vector<pid_t>()));

  EXPECT_CALL(mock_kernel_.Mock(), Usleep(
      FLAGS_lmctfy_ms_delay_between_kills * 1000))
      .WillRepeatedly(Return(0));

  EXPECT_TRUE(container_->KillAll().ok());
}

TEST_F(ContainerImplTest, KillAllDieOnFirstKill) {
  const vector<pid_t> kPids = {1, 2, 3};

  // Processes are killed on the first SIGKILL.
  EXPECT_CALL(*mock_tasks_handler_, ListProcesses())
      .WillOnce(Return(kPids))
      .WillRepeatedly(Return(vector<pid_t>()));
  ExpectKill(kPids, 0, Exactly(1));

  // No threads.
  EXPECT_CALL(*mock_tasks_handler_, ListThreads())
      .WillRepeatedly(Return(vector<pid_t>()));

  EXPECT_CALL(mock_kernel_.Mock(), Usleep(
      FLAGS_lmctfy_ms_delay_between_kills * 1000))
      .WillRepeatedly(Return(0));

  EXPECT_TRUE(container_->KillAll().ok());
}

TEST_F(ContainerImplTest, KillAllSigkillFails) {
  const vector<pid_t> kPids = {1, 2, 3};

  // Processes are killed on the first SIGTERM.
  EXPECT_CALL(*mock_tasks_handler_, ListProcesses())
      .WillOnce(Return(kPids))
      .WillRepeatedly(Return(vector<pid_t>()));
  ExpectKill(kPids, -1, Exactly(1));

  // No threads.
  EXPECT_CALL(*mock_tasks_handler_, ListThreads())
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
  EXPECT_CALL(*mock_tasks_handler_, ListProcesses())
      .WillOnce(Return(kPids))
      .WillOnce(Return(kPids))
      .WillRepeatedly(Return(vector<pid_t>()));
  ExpectKill(kPids, 0, Exactly(2));

  // No threads.
  EXPECT_CALL(*mock_tasks_handler_, ListThreads())
      .WillRepeatedly(Return(vector<pid_t>()));

  EXPECT_CALL(mock_kernel_.Mock(), Usleep(
      FLAGS_lmctfy_ms_delay_between_kills * 1000))
      .WillRepeatedly(Return(0));

  EXPECT_TRUE(container_->KillAll().ok());
}

TEST_F(ContainerImplTest, KillAllUsleepFails) {
  const vector<pid_t> kPids = {1, 2, 3};

  // Processes are killed after the second SIGKILL.
  EXPECT_CALL(*mock_tasks_handler_, ListProcesses())
      .WillOnce(Return(kPids))
      .WillOnce(Return(kPids))
      .WillRepeatedly(Return(vector<pid_t>()));
  ExpectKill(kPids, 0, Exactly(2));

  // No threads.
  EXPECT_CALL(*mock_tasks_handler_, ListThreads())
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
  EXPECT_CALL(*mock_tasks_handler_, ListProcesses())
      .WillRepeatedly(Return(kPids));
  ExpectKill(kPids, 0, AtLeast(1));

  // No threads.
  EXPECT_CALL(*mock_tasks_handler_, ListThreads())
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
  EXPECT_CALL(*mock_tasks_handler_, ListProcesses())
      .WillOnce(Return(kPids))
      .WillRepeatedly(Return(vector<pid_t>()));
  ExpectKill(kPids, 0, Exactly(1));

  // Threads are killed on the first SIGKILL.
  EXPECT_CALL(*mock_tasks_handler_, ListThreads())
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
  EXPECT_CALL(*mock_tasks_handler_, ListProcesses())
      .WillOnce(Return(kPids))
      .WillRepeatedly(Return(vector<pid_t>()));
  ExpectKill(kPids, 0, Exactly(1));

  // Threads are killed on the first SIGKILL.
  EXPECT_CALL(*mock_tasks_handler_, ListThreads())
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
  EXPECT_CALL(*mock_tasks_handler_, ListProcesses())
      .WillOnce(Return(kPids))
      .WillRepeatedly(Return(vector<pid_t>()));
  ExpectKill(kPids, 0, Exactly(1));

  // Threads are never killed.
  EXPECT_CALL(*mock_tasks_handler_, ListThreads())
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

  EXPECT_CALL(*mock_tasks_handler_, ListThreads())
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

  EXPECT_CALL(*mock_tasks_handler_, ListThreads())
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

  EXPECT_CALL(*mock_tasks_handler_, ListThreads())
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

  EXPECT_CALL(*mock_tasks_handler_, ListThreads())
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

  EXPECT_CALL(*mock_tasks_handler_, ListThreads())
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED, CallKillTasks(ContainerImpl::LIST_THREADS));
}

TEST_F(ContainerImplTest, KillTasksThreadsKillFails) {
  const vector<pid_t> kPids = {1, 2, 3};

  EXPECT_CALL(*mock_tasks_handler_, ListThreads())
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

  EXPECT_CALL(*mock_tasks_handler_, ListProcesses())
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

  EXPECT_CALL(*mock_tasks_handler_, ListProcesses())
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

  EXPECT_CALL(*mock_tasks_handler_, ListProcesses())
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

  EXPECT_CALL(*mock_tasks_handler_, ListProcesses())
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

  EXPECT_CALL(*mock_tasks_handler_, ListProcesses())
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED, CallKillTasks(ContainerImpl::LIST_PROCESSES));
}

TEST_F(ContainerImplTest, KillTasksProcessesKillFails) {
  const vector<pid_t> kPids = {1, 2, 3};

  EXPECT_CALL(*mock_tasks_handler_, ListProcesses())
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

TEST_F(ContainerImplTest, EnterNoTids) {
  vector<pid_t> tids;

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

  EXPECT_CALL(*mock_tasks_handler_, TrackTasks(tids))
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED, container_->Enter(tids));
}

TEST_F(ContainerImplTest, EnterGetResourceHandlersFails) {
  vector<pid_t> tids = {1, 2, 3};

  EXPECT_CALL(*mock_tasks_handler_, TrackTasks(tids))
      .WillRepeatedly(Return(Status::OK));

  // Enter() takes ownership of the return of GetResourceHandlers().
  ExpectGetResourceHandlers(Status::CANCELLED);

  EXPECT_EQ(Status::CANCELLED, container_->Enter(tids));
}

TEST_F(ContainerImplTest, EnterResourceHandlerEnterFails) {
  vector<pid_t> tids = {1, 2, 3};

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

  EXPECT_CALL(*mock_tasks_handler_, Destroy())
      .WillOnce(Return(Status::OK));

  // Destroy() should delete the handlers.
  unique_ptr<MockTasksHandler> d1(mock_tasks_handler_);
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

  EXPECT_CALL(*mock_tasks_handler_, Destroy())
      .WillOnce(Return(Status::OK));

  // Destroy() should delete the handlers.
  unique_ptr<MockTasksHandler> d1(mock_tasks_handler_);
  unique_ptr<MockResourceHandler> d3(mock_handler1);
  unique_ptr<MockResourceHandler> d4(mock_handler3);

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
  // Handlers are deleted by Destroy().
  ElementDeleter d(&to_delete);

  EXPECT_CALL(*mock_tasks_handler_, Destroy())
      .WillOnce(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED, container_->Destroy());
}

// Tests for Run().

static const pid_t kTid = 42;
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

  EXPECT_CALL(*mock_subprocess_, SetArgv(kCmd))
      .Times(1);
  EXPECT_CALL(*mock_subprocess_, SetUseSession())
      .Times(1);
  EXPECT_CALL(*mock_subprocess_, Start())
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_subprocess_, pid())
      .WillRepeatedly(Return(kPid));

  EXPECT_CALL(mock_kernel_.Mock(), GetTID())
      .WillRepeatedly(Return(kTid));

  ExpectEnter(kTid, Status::OK);

  RunSpec spec;
  spec.set_fd_policy(RunSpec::DETACHED);
  StatusOr<pid_t> statusor = container_->Run(kCmd, spec);
  ASSERT_TRUE(statusor.ok());
  EXPECT_EQ(kPid, statusor.ValueOrDie());
}

TEST_F(ContainerImplTest, RunSuccessForeground) {
  const vector<string> kCmd = {"/bin/echo", "test", "cmd"};

  EXPECT_CALL(*mock_subprocess_, SetArgv(kCmd))
      .Times(1);
  EXPECT_CALL(*mock_subprocess_,
              SetChannelAction(CHAN_STDIN, ACTION_DUPPARENT))
      .Times(1);
  EXPECT_CALL(*mock_subprocess_,
              SetChannelAction(CHAN_STDOUT, ACTION_DUPPARENT))
      .Times(1);
  EXPECT_CALL(*mock_subprocess_,
              SetChannelAction(CHAN_STDERR, ACTION_DUPPARENT))
      .Times(1);
  EXPECT_CALL(*mock_subprocess_, Start())
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_subprocess_, pid())
      .WillRepeatedly(Return(kPid));

  EXPECT_CALL(mock_kernel_.Mock(), GetTID())
      .WillRepeatedly(Return(kTid));

  ExpectEnter(kTid, Status::OK);

  RunSpec spec;
  spec.set_fd_policy(RunSpec::INHERIT);
  StatusOr<pid_t> statusor = container_->Run(kCmd, spec);
  ASSERT_TRUE(statusor.ok());
  EXPECT_EQ(kPid, statusor.ValueOrDie());
}

TEST_F(ContainerImplTest, RunSuccessDefaultPolicy) {
  const vector<string> kCmd = {"/bin/echo", "test", "cmd"};

  EXPECT_CALL(*mock_subprocess_, SetArgv(kCmd))
      .Times(1);
  EXPECT_CALL(*mock_subprocess_,
              SetChannelAction(CHAN_STDIN, ACTION_DUPPARENT))
      .Times(1);
  EXPECT_CALL(*mock_subprocess_,
              SetChannelAction(CHAN_STDOUT, ACTION_DUPPARENT))
      .Times(1);
  EXPECT_CALL(*mock_subprocess_,
              SetChannelAction(CHAN_STDERR, ACTION_DUPPARENT))
      .Times(1);
  EXPECT_CALL(*mock_subprocess_, Start())
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_subprocess_, pid())
      .WillRepeatedly(Return(kPid));

  EXPECT_CALL(mock_kernel_.Mock(), GetTID())
      .WillRepeatedly(Return(kTid));

  ExpectEnter(kTid, Status::OK);

  // Inherit is the default policy.
  RunSpec spec;
  StatusOr<pid_t> statusor = container_->Run(kCmd, spec);
  ASSERT_TRUE(statusor.ok());
  EXPECT_EQ(kPid, statusor.ValueOrDie());
}

TEST_F(ContainerImplTest, RunEnterFails) {
  const vector<string> kCmd = {"/bin/echo", "test", "cmd"};

  EXPECT_CALL(*mock_subprocess_, SetArgv(kCmd))
      .Times(1);
  EXPECT_CALL(*mock_subprocess_, SetUseSession())
      .Times(1);

  EXPECT_CALL(mock_kernel_.Mock(), GetTID())
      .WillRepeatedly(Return(kTid));

  ExpectEnter(kTid, Status::CANCELLED);

  RunSpec spec;
  spec.set_fd_policy(RunSpec::DETACHED);
  EXPECT_EQ(Status::CANCELLED, container_->Run(kCmd, spec).status());
}

TEST_F(ContainerImplTest, RunStartProcessFails) {
  const vector<string> kCmd = {"/bin/echo", "test", "cmd"};

  EXPECT_CALL(*mock_subprocess_, SetArgv(kCmd))
      .Times(1);
  EXPECT_CALL(*mock_subprocess_, SetUseSession())
      .Times(1);
  EXPECT_CALL(*mock_subprocess_, Start())
      .WillOnce(Return(false));

  EXPECT_CALL(mock_kernel_.Mock(), GetTID())
      .WillRepeatedly(Return(kTid));

  ExpectEnter(kTid, Status::OK);

  RunSpec spec;
  spec.set_fd_policy(RunSpec::DETACHED);
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

  // None should be updated.

  EXPECT_TRUE(container_->Update(spec, Container::UPDATE_DIFF).ok());
}

TEST_F(ContainerImplTest, UpdateDiffNonExistingResource) {
  ContainerSpec spec;

  // Specify filesystem which is not attached to this container.
  spec.mutable_filesystem()->set_fd_limit(100);

  // Returns CPU, MEMORY, and FILESYSTEM ResourceHandlers.
  ExpectGetResourceHandlers(Status::OK);

  Status status = container_->Update(spec, Container::UPDATE_DIFF);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(::util::error::INVALID_ARGUMENT, status.error_code());
}

TEST_F(ContainerImplTest, UpdateDiffGetResourceHandlersFails) {
  ContainerSpec spec;

  ExpectGetResourceHandlers(Status::CANCELLED);

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

  // Returns CPU, MEMORY, and FILESYSTEM ResourceHandlers.
  vector<MockResourceHandler *> mock_handlers = ExpectGetResourceHandlers(
      Status::OK);

  // Expect all resources of this container to be updated (filesystem is not in
  // this container).
  for (MockResourceHandler *mock_handler : mock_handlers) {
    if (mock_handler->type() != RESOURCE_FILESYSTEM) {
      EXPECT_CALL(*mock_handler, Update(EqualsInitializedProto(spec),
                                        Container::UPDATE_REPLACE))
          .WillOnce(Return(Status::OK));
    }
  }

  EXPECT_TRUE(container_->Update(spec, Container::UPDATE_REPLACE).ok());
}

TEST_F(ContainerImplTest, UpdateReplaceSomeResourcesMissing) {
  ContainerSpec spec;
  spec.mutable_cpu()->set_limit(2000);

  // Returns CPU, MEMORY, and FILESYSTEM ResourceHandlers.
  vector<MockResourceHandler *> mock_handlers = ExpectGetResourceHandlers(
      Status::OK);

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

  EXPECT_FALSE(container_->Update(spec, Container::UPDATE_REPLACE).ok());
}

TEST_F(ContainerImplTest, UpdateReplaceSomeResourcesExtraAndSomeMissing) {
  ContainerSpec spec;
  spec.mutable_cpu()->set_limit(2000);
  spec.mutable_filesystem();

  // Returns CPU, MEMORY, and FILESYSTEM ResourceHandlers.
  vector<MockResourceHandler *> mock_handlers = ExpectGetResourceHandlers(
      Status::OK);

  EXPECT_FALSE(container_->Update(spec, Container::UPDATE_REPLACE).ok());
}

TEST_F(ContainerImplTest, UpdateReplaceGetResourceHandlersFails) {
  ContainerSpec spec;

  ExpectGetResourceHandlers(Status::CANCELLED);

  EXPECT_EQ(Status::CANCELLED,
            container_->Update(spec, Container::UPDATE_REPLACE));
}

TEST_F(ContainerImplTest, UpdateReplaceUpdateFails) {
  ContainerSpec spec;
  spec.mutable_cpu()->set_limit(2000);
  spec.mutable_memory()->set_limit(100);

  // Returns CPU, MEMORY, and FILESYSTEM ResourceHandlers.
  vector<MockResourceHandler *> mock_handlers = ExpectGetResourceHandlers(
      Status::OK);

  // Expect all resources of this container to be updated (filesystem is not in
  // this container).
  for (MockResourceHandler *mock_handler : mock_handlers) {
    if (mock_handler->type() != RESOURCE_FILESYSTEM) {
      EXPECT_CALL(*mock_handler, Update(EqualsInitializedProto(spec),
                                        Container::UPDATE_REPLACE))
          .WillRepeatedly(Return(Status::CANCELLED));
    }
  }

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

  // No handler can register a notification for the event.
  for (MockResourceHandler *mock_handler : mock_handlers) {
    EXPECT_CALL(*mock_handler,
                RegisterNotification(EqualsInitializedProto(spec), NotNull()))
        .WillRepeatedly(DoAll(Invoke(&DeleteCallback),
                              Return(Status(::util::error::NOT_FOUND, ""))));
  }

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

  StatusOr<ContainerSpec> statusor = container_->Spec();
  ASSERT_OK(statusor);
  ContainerSpec stats = statusor.ValueOrDie();
  EXPECT_TRUE(stats.has_cpu());
  EXPECT_TRUE(stats.has_memory());
  EXPECT_FALSE(stats.has_diskio());
  EXPECT_FALSE(stats.has_network());
  EXPECT_FALSE(stats.has_monitoring());
  EXPECT_FALSE(stats.has_filesystem());
}

TEST_F(ContainerImplTest, SpecEmpty) {
  vector<MockResourceHandler *> mock_handlers =
      ExpectGetResourceHandlers(Status::OK);

  for (MockResourceHandler *mock_handler : mock_handlers) {
    if ((mock_handler->type() == RESOURCE_CPU) ||
        (mock_handler->type() == RESOURCE_MEMORY)) {
      EXPECT_CALL(*mock_handler, Spec(NotNull()))
          .WillOnce(Return(Status::OK));
    }
  }

  StatusOr<ContainerSpec> statusor = container_->Spec();
  ASSERT_OK(statusor);
  ContainerSpec stats = statusor.ValueOrDie();
  EXPECT_FALSE(stats.has_cpu());
  EXPECT_FALSE(stats.has_memory());
  EXPECT_FALSE(stats.has_diskio());
  EXPECT_FALSE(stats.has_network());
  EXPECT_FALSE(stats.has_monitoring());
  EXPECT_FALSE(stats.has_filesystem());
}

TEST_F(ContainerImplTest, SpecGetResourceHandlersFails) {
  vector<MockResourceHandler *> mock_handlers =
      ExpectGetResourceHandlers(Status::CANCELLED);

  EXPECT_EQ(Status::CANCELLED, container_->Spec().status());
}

TEST_F(ContainerImplTest, SpecResourceSpecFails) {
  vector<MockResourceHandler *> mock_handlers =
      ExpectGetResourceHandlers(Status::OK);

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
  ExpectEnter(0, Status::OK);
  EXPECT_CALL(mock_kernel_.Mock(), SetITimer(ITIMER_REAL, nullptr, nullptr))
      .WillOnce(Return(0));
  EXPECT_CALL(mock_kernel_.Mock(), SetITimer(ITIMER_VIRTUAL, nullptr, nullptr))
      .WillOnce(Return(0));
  EXPECT_CALL(mock_kernel_.Mock(), SetITimer(ITIMER_PROF, nullptr, nullptr))
      .WillOnce(Return(0));
  EXPECT_CALL(mock_kernel_.Mock(), Execvp(kCmd[0], kCmd))
      .WillOnce(Return(0));

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
  ExpectEnter(0, Status::CANCELLED);
  EXPECT_CALL(mock_kernel_.Mock(), SetITimer(ITIMER_REAL, nullptr, nullptr))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(mock_kernel_.Mock(), SetITimer(ITIMER_VIRTUAL, nullptr, nullptr))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(mock_kernel_.Mock(), SetITimer(ITIMER_PROF, nullptr, nullptr))
      .WillRepeatedly(Return(0));

  EXPECT_EQ(Status::CANCELLED, container_->Exec(kCmd));
}

TEST_F(ContainerImplTest, ExecSetITimerFails) {
  const vector<string> kCmd = {"/bin/echo", "test", "cmd"};

  ExpectExists(true);
  ExpectEnter(0, Status::OK);
  EXPECT_CALL(mock_kernel_.Mock(), SetITimer(ITIMER_REAL, nullptr, nullptr))
      .WillOnce(Return(-1));
  EXPECT_CALL(mock_kernel_.Mock(), SetITimer(ITIMER_VIRTUAL, nullptr, nullptr))
      .WillOnce(Return(-1));
  EXPECT_CALL(mock_kernel_.Mock(), SetITimer(ITIMER_PROF, nullptr, nullptr))
      .WillOnce(Return(-1));
  EXPECT_CALL(mock_kernel_.Mock(), Execvp(kCmd[0], kCmd))
      .WillOnce(Return(0));

  // We expect INTERNAL since Exec does not typically return on success and we
  // ignore the failure of SetITimer.
  EXPECT_ERROR_CODE(::util::error::INTERNAL, container_->Exec(kCmd));
}

TEST_F(ContainerImplTest, ExecExecvpFails) {
  const vector<string> kCmd = {"/bin/echo", "test", "cmd"};

  ExpectExists(true);
  ExpectEnter(0, Status::OK);
  EXPECT_CALL(mock_kernel_.Mock(), SetITimer(ITIMER_REAL, nullptr, nullptr))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(mock_kernel_.Mock(), SetITimer(ITIMER_VIRTUAL, nullptr, nullptr))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(mock_kernel_.Mock(), SetITimer(ITIMER_PROF, nullptr, nullptr))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(mock_kernel_.Mock(), Execvp(kCmd[0], kCmd))
      .WillOnce(Return(1));

  EXPECT_ERROR_CODE(::util::error::INTERNAL, container_->Exec(kCmd));
}


TEST_F(ContainerImplTest, Name) {
  EXPECT_EQ(kContainerName, container_->name());
}

}  // namespace lmctfy
}  // namespace containers
