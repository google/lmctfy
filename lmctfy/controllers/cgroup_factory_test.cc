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

#include "lmctfy/controllers/cgroup_factory.h"

#include <unistd.h>
#include <memory>

#include "base/callback.h"
#include "system_api/kernel_api_mock.h"
#include "util/errors_test_util.h"
#include "util/file_lines_test_util.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "util/task/codes.pb.h"
#include "util/task/status.h"

using ::system_api::KernelAPIMock;
using ::util::FileLinesTestUtil;
using ::std::map;
using ::std::unique_ptr;
using ::testing::Contains;
using ::testing::DoAll;
#include "util/testing/equals_initialized_proto.h"
using ::testing::EqualsInitializedProto;
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::NotNull;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::StrEq;
using ::testing::StrictMock;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace lmctfy {

// To initialize the global supported hierarchies.
namespace internal {
extern void InitSupportedHierarchies();
}  // namespace internal

static const char kCgroupMountType[] = "cgroup";
static const CgroupHierarchy kType = CGROUP_MEMORY;
static const CgroupHierarchy kTypeNotOwns = CGROUP_CPUACCT;
static const char kCgroupPath[] = "/dev/cgroup/memory/test";
static const char kCgroupPathNotOwns[] = "/dev/cgroup/cpu/test";
static const char kContainerName[] = "/test";
static const char kCpuMount[] = "/dev/cgroup/cpu";
static const char kMemoryMount[] = "/dev/cgroup/memory";
static const char kNetMount[] = "/dev/cgroup/net";
static const char kDevCgroup[] = "/dev/cgroup";
static const char kDev[] = "/dev";
static const char kProcCgroups[] = "/proc/cgroups";

// Expect the mounted hierarchy to be of type net.
static void CheckNet(const string &name, const string &path,
                     const string &fstype, uint64 flags, const void *data) {
  EXPECT_THAT("net", StrEq(static_cast<const char *>(data)));
}

// Expect the mounted hierarchy to be of type net,rlimit.
static void CheckNetAndRlimit(const string &name, const string &path,
                        const string &fstype, uint64 flags, const void *data) {
  EXPECT_THAT("net,rlimit", StrEq(static_cast<const char *>(data)));
}

class CgroupFactoryTest : public ::testing::Test {
 public:
  CgroupFactoryTest() {
    internal::InitSupportedHierarchies();
  }

  virtual void SetUp() {
    mock_kernel_.reset(new StrictMock<KernelAPIMock>());
    factory_.reset(
        new CgroupFactory({{CGROUP_CPU, kCpuMount}, {CGROUP_CPUACCT, kCpuMount},
                           {CGROUP_MEMORY, kMemoryMount}},
                          mock_kernel_.get()));
  }

  // Checks that the specified mount exists in the factory's internal map.
  void CheckMount(CgroupHierarchy hierarchy, const string &path, bool owns) {
    auto path_mount_it = factory_->mount_paths_.find(hierarchy);
    ASSERT_NE(path_mount_it, factory_->mount_paths_.end());

    EXPECT_EQ(hierarchy, path_mount_it->first);
    EXPECT_EQ(path, path_mount_it->second.path);
    EXPECT_EQ(owns, path_mount_it->second.owns);

    EXPECT_TRUE(factory_->IsMounted(hierarchy));
  }

  // Wrappers used for testing.

  const map<CgroupHierarchy, CgroupFactory::MountPoint> &GetMountPaths(
      CgroupFactory *factory) {
    return factory->mount_paths_;
  }

 protected:
  FileLinesTestUtil mock_lines_;
  unique_ptr<KernelAPIMock> mock_kernel_;
  unique_ptr<CgroupFactory> factory_;
};

// Tests for OwnsCgroup().

TEST_F(CgroupFactoryTest, OwnsCgroup) {
  EXPECT_TRUE(factory_->OwnsCgroup(CGROUP_CPU));
  EXPECT_FALSE(factory_->OwnsCgroup(CGROUP_CPUACCT));
  EXPECT_TRUE(factory_->OwnsCgroup(CGROUP_MEMORY));
  EXPECT_FALSE(factory_->OwnsCgroup(CGROUP_JOB));
}

// Tests for New().

TEST_F(CgroupFactoryTest, New) {
  mock_lines_.ExpectFileLines(
      "/proc/mounts",
      {"rootfs / rootfs rw 0 0",
       "none /dev/cgroup/rlimit cgroup rw,relatime,rlimit 0 0",
       "none /dev/cgroup/memory cgroup rw,relatime,memory 0 0",
       "sysfs /sys sysfs rw,nosuid,nodev,noexec,relatime 0 0",
       "none /dev/cgroup/cpu cgroup rw,relatime,cpuacct,cpu 0 0",
       "proc /proc proc rw,nosuid,nodev,noexec,relatime 0 0",
       "none /fs/another/net cgroup rw,relatime,net 0 0", });

  // CPU, memory, and net are accessible, rlimit is not.
  EXPECT_CALL(*mock_kernel_, Access("/dev/cgroup/cpu", R_OK))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, Access("/dev/cgroup/memory", R_OK))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, Access("/fs/another/net", R_OK))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, Access("/dev/cgroup/rlimit", R_OK))
      .WillRepeatedly(Return(1));

  StatusOr<CgroupFactory *> statusor = CgroupFactory::New(mock_kernel_.get());
  ASSERT_OK(statusor);
  ASSERT_NE(nullptr, statusor.ValueOrDie());
  unique_ptr<CgroupFactory> factory(statusor.ValueOrDie());

  const auto &mount_paths = GetMountPaths(factory.get());
  ASSERT_EQ(4, mount_paths.size());

  EXPECT_EQ("/dev/cgroup/cpu", mount_paths.at(CGROUP_CPU).path);
  EXPECT_TRUE(mount_paths.at(CGROUP_CPU).owns);

  EXPECT_EQ("/dev/cgroup/cpu", mount_paths.at(CGROUP_CPUACCT).path);
  EXPECT_FALSE(mount_paths.at(CGROUP_CPUACCT).owns);

  EXPECT_EQ("/dev/cgroup/memory", mount_paths.at(CGROUP_MEMORY).path);
  EXPECT_TRUE(mount_paths.at(CGROUP_MEMORY).owns);

  EXPECT_EQ("/fs/another/net", mount_paths.at(CGROUP_NET).path);
  EXPECT_TRUE(mount_paths.at(CGROUP_NET).owns);
}

// Tests for supported hierarchy mapping.

TEST_F(CgroupFactoryTest, SupportedHierarchyMapping) {
  EXPECT_EQ("cpu", factory_->GetHierarchyName(CGROUP_CPU));
  EXPECT_EQ("cpuacct", factory_->GetHierarchyName(CGROUP_CPUACCT));
  EXPECT_EQ("cpuset", factory_->GetHierarchyName(CGROUP_CPUSET));
  EXPECT_EQ("freezer", factory_->GetHierarchyName(CGROUP_FREEZER));
  EXPECT_EQ("job", factory_->GetHierarchyName(CGROUP_JOB));
  EXPECT_EQ("memory", factory_->GetHierarchyName(CGROUP_MEMORY));
  EXPECT_EQ("net", factory_->GetHierarchyName(CGROUP_NET));
  EXPECT_EQ("blkio", factory_->GetHierarchyName(CGROUP_BLOCKIO));
  EXPECT_EQ("perf_event", factory_->GetHierarchyName(CGROUP_PERF_EVENT));
  EXPECT_EQ("rlimit", factory_->GetHierarchyName(CGROUP_RLIMIT));
  EXPECT_EQ("",
            factory_->GetHierarchyName(static_cast<CgroupHierarchy>(10000)));
}

// Tests for Get().

TEST_F(CgroupFactoryTest, GetSuccess) {
  EXPECT_CALL(*mock_kernel_, Access(kCgroupPath, F_OK))
      .WillRepeatedly(Return(0));

  StatusOr<string> statusor = factory_->Get(kType, kContainerName);
  ASSERT_TRUE(statusor.ok());
  EXPECT_EQ(kCgroupPath, statusor.ValueOrDie());
}

TEST_F(CgroupFactoryTest, GetCgroupDoesNotExist) {
  EXPECT_CALL(*mock_kernel_, Access(kCgroupPath, F_OK))
      .WillRepeatedly(Return(-1));

  StatusOr<string> statusor = factory_->Get(kType, kContainerName);
  EXPECT_FALSE(statusor.ok());
  EXPECT_EQ(::util::error::NOT_FOUND, statusor.status().error_code());
}

// Tests for Create().

TEST_F(CgroupFactoryTest, CreateSuccess) {
  EXPECT_CALL(*mock_kernel_, Access(kCgroupPath, F_OK))
      .WillRepeatedly(Return(-1));
  EXPECT_CALL(*mock_kernel_, MkDir(kCgroupPath)).WillRepeatedly(Return(0));

  StatusOr<string> statusor = factory_->Create(kType, kContainerName);
  ASSERT_TRUE(statusor.ok());
  EXPECT_EQ(kCgroupPath, statusor.ValueOrDie());
}

TEST_F(CgroupFactoryTest, CreateSuccessDoesNotOwnCgroup) {
  EXPECT_CALL(*mock_kernel_, Access(kCgroupPathNotOwns, F_OK))
      .WillRepeatedly(Return(0));

  StatusOr<string> statusor = factory_->Create(kTypeNotOwns, kContainerName);
  ASSERT_TRUE(statusor.ok());
  EXPECT_EQ(kCgroupPathNotOwns, statusor.ValueOrDie());
}

TEST_F(CgroupFactoryTest, CreateDoesNotOwnCgroupCgroupDoesNotExist) {
  EXPECT_CALL(*mock_kernel_, Access(kCgroupPathNotOwns, F_OK))
      .WillRepeatedly(Return(-1));

  StatusOr<string> statusor = factory_->Create(kTypeNotOwns, kContainerName);
  EXPECT_FALSE(statusor.ok());
  EXPECT_EQ(::util::error::NOT_FOUND, statusor.status().error_code());
}

TEST_F(CgroupFactoryTest, CreateCgroupAlreadyExists) {
  EXPECT_CALL(*mock_kernel_, Access(kCgroupPath, F_OK))
      .WillRepeatedly(Return(0));

  StatusOr<string> statusor = factory_->Create(kType, kContainerName);
  EXPECT_FALSE(statusor.ok());
  EXPECT_EQ(::util::error::ALREADY_EXISTS, statusor.status().error_code());
}

TEST_F(CgroupFactoryTest, CreateMkdirFails) {
  EXPECT_CALL(*mock_kernel_, Access(kCgroupPath, F_OK))
      .WillRepeatedly(Return(-1));
  EXPECT_CALL(*mock_kernel_, MkDir(kCgroupPath))
      .WillRepeatedly(Return(-1));

  StatusOr<string> statusor = factory_->Create(kType, kContainerName);
  EXPECT_FALSE(statusor.ok());
  EXPECT_EQ(::util::error::FAILED_PRECONDITION, statusor.status().error_code());
}

// Tests for IsMounted().

TEST_F(CgroupFactoryTest, IsMounted) {
  EXPECT_TRUE(factory_->IsMounted(CGROUP_CPU));
  EXPECT_TRUE(factory_->IsMounted(CGROUP_CPUACCT));
  EXPECT_FALSE(factory_->IsMounted(CGROUP_CPUSET));
  EXPECT_TRUE(factory_->IsMounted(CGROUP_MEMORY));
  EXPECT_FALSE(factory_->IsMounted(CGROUP_BLOCKIO));
  EXPECT_FALSE(factory_->IsMounted(CGROUP_NET));
  EXPECT_FALSE(factory_->IsMounted(CGROUP_PERF_EVENT));
}

// Tests for Mount().

TEST_F(CgroupFactoryTest, MountNew) {
  CgroupMount cgroup;
  cgroup.set_mount_path(kNetMount);
  cgroup.add_hierarchy(CGROUP_NET);

  EXPECT_CALL(*mock_kernel_, MkDirRecursive(kNetMount))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, Mount(kCgroupMountType, kNetMount,
                                   kCgroupMountType, 0, NotNull()))
      .WillOnce(
           DoAll(Invoke(&CheckNet), Return(0)));

  EXPECT_OK(factory_->Mount(cgroup));
  CheckMount(CGROUP_NET, kNetMount, true);
}

TEST_F(CgroupFactoryTest, MountExisting) {
  CgroupMount cgroup;
  cgroup.set_mount_path(kMemoryMount);
  cgroup.add_hierarchy(CGROUP_MEMORY);

  EXPECT_TRUE(factory_->Mount(cgroup).ok());
  CheckMount(CGROUP_MEMORY, kMemoryMount, true);
}

TEST_F(CgroupFactoryTest, MountMultipleExisting) {
  CgroupMount cgroup;
  cgroup.set_mount_path(kCpuMount);
  cgroup.add_hierarchy(CGROUP_CPU);
  cgroup.add_hierarchy(CGROUP_CPUACCT);

  EXPECT_TRUE(factory_->Mount(cgroup).ok());
  CheckMount(CGROUP_CPU, kCpuMount, true);
  CheckMount(CGROUP_CPUACCT, kCpuMount, false);
}

TEST_F(CgroupFactoryTest, MountNewForExistingMount) {
  CgroupMount cgroup;
  cgroup.set_mount_path(kCpuMount);
  cgroup.add_hierarchy(CGROUP_NET);

  EXPECT_FALSE(factory_->Mount(cgroup).ok());
}

TEST_F(CgroupFactoryTest, MountMultipleHierarchies) {
  CgroupMount cgroup;
  cgroup.set_mount_path(kNetMount);
  cgroup.add_hierarchy(CGROUP_NET);
  cgroup.add_hierarchy(CGROUP_RLIMIT);

  EXPECT_CALL(*mock_kernel_, MkDirRecursive(kNetMount))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, Mount(kCgroupMountType, kNetMount,
                                   kCgroupMountType, 0, NotNull()))
      .WillOnce(DoAll(Invoke(&CheckNetAndRlimit), Return(0)));

  EXPECT_TRUE(factory_->Mount(cgroup).ok());
  CheckMount(CGROUP_NET, kNetMount, true);
  CheckMount(CGROUP_RLIMIT, kNetMount, false);
}

TEST_F(CgroupFactoryTest, MountMultipleHierarchiesSomeAlreadyMounted) {
  CgroupMount cgroup;
  cgroup.set_mount_path(kCpuMount);
  cgroup.add_hierarchy(CGROUP_CPU);
  cgroup.add_hierarchy(CGROUP_RLIMIT);

  string type;
  EXPECT_CALL(*mock_kernel_, MkDirRecursive(kCpuMount))
      .WillRepeatedly(Return(0));

  EXPECT_EQ(::util::error::INVALID_ARGUMENT,
            factory_->Mount(cgroup).error_code());
}

TEST_F(CgroupFactoryTest, MountFails) {
  CgroupMount cgroup;
  cgroup.set_mount_path(kNetMount);
  cgroup.add_hierarchy(CGROUP_NET);

  string type;
  EXPECT_CALL(*mock_kernel_, MkDirRecursive(kNetMount))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, Mount(kCgroupMountType, kNetMount,
                                   kCgroupMountType, 0, NotNull()))
      .WillOnce(Return(1));

  Status status = factory_->Mount(cgroup);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(::util::error::FAILED_PRECONDITION, status.error_code());
}

TEST_F(CgroupFactoryTest, MountRemountInNewPlace) {
  CgroupMount cgroup;
  cgroup.set_mount_path(kNetMount);
  cgroup.add_hierarchy(CGROUP_CPU);

  Status status = factory_->Mount(cgroup);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(::util::error::INVALID_ARGUMENT, status.error_code());
}

TEST_F(CgroupFactoryTest, MountMkDirFails) {
  CgroupMount cgroup;
  cgroup.set_mount_path(kNetMount);
  cgroup.add_hierarchy(CGROUP_NET);

  EXPECT_CALL(*mock_kernel_, MkDirRecursive(kNetMount))
      .WillRepeatedly(Return(-1));

  EXPECT_EQ(::util::error::FAILED_PRECONDITION,
            factory_->Mount(cgroup).error_code());
}

// Tests for DetectCgroupPath().

TEST_F(CgroupFactoryTest, DetectCgroupPathSuccess) {
  mock_lines_.ExpectFileLines("/proc/self/cgroup",
                              {"7:net:/sys\n", "6:memory:/sys/subcont\n"});

  StatusOr<string> statusor = factory_->DetectCgroupPath(0, CGROUP_MEMORY);
  ASSERT_OK(statusor);
  EXPECT_EQ("/sys/subcont", statusor.ValueOrDie());
}

TEST_F(CgroupFactoryTest, DetectCgroupPathProcCgroupIsEmpty) {
  mock_lines_.ExpectFileLines("/proc/self/cgroup", {});

  EXPECT_ERROR_CODE(::util::error::NOT_FOUND,
                    factory_->DetectCgroupPath(0, CGROUP_MEMORY));
}

TEST_F(CgroupFactoryTest, DetectCgroupPathCgroupHierarchyIsNotMounted) {
  mock_lines_.ExpectFileLines("/proc/self/cgroup", {});

  EXPECT_ERROR_CODE(::util::error::NOT_FOUND,
                    factory_->DetectCgroupPath(0, CGROUP_BLOCKIO));
}

TEST_F(CgroupFactoryTest, DetectCgroupPathWithTidSuccess) {
  mock_lines_.ExpectFileLines("/proc/12/cgroup",
                              {"7:net:/sys\n", "6:memory:/sys/subcont\n"});

  StatusOr<string> statusor = factory_->DetectCgroupPath(12, CGROUP_MEMORY);
  ASSERT_OK(statusor);
  EXPECT_EQ("/sys/subcont", statusor.ValueOrDie());
}

TEST_F(CgroupFactoryTest, DetectCgroupPathLineHasBadFormat) {
  mock_lines_.ExpectFileLines("/proc/self/cgroup",
                              {"7:net:/sys\n", "6memory/sys/subcont\n"});

  EXPECT_ERROR_CODE(::util::error::NOT_FOUND,
                    factory_->DetectCgroupPath(0, CGROUP_MEMORY));
}

TEST_F(CgroupFactoryTest, DetectCgroupPathLineHasMoreElementsThanExpected) {
  mock_lines_.ExpectFileLines(
      "/proc/self/cgroup", {"7:net:/sys:new\n", "6:memory:/sys/subcont:new\n"});

  EXPECT_ERROR_CODE(::util::error::NOT_FOUND,
                    factory_->DetectCgroupPath(0, CGROUP_MEMORY));
}

TEST_F(CgroupFactoryTest, DetectCgroupPathComountedSubsystems) {
  mock_lines_.ExpectFileLines(
      "/proc/self/cgroup",
      {"7:net:/sys\n", "6:memory,cpu,cpuacct:/sys/subcont\n"});

  StatusOr<string> statusor = factory_->DetectCgroupPath(0, CGROUP_MEMORY);
  ASSERT_OK(statusor);
  EXPECT_EQ("/sys/subcont", statusor.ValueOrDie());
}

TEST_F(CgroupFactoryTest, DetectCgroupPathSubsystemNotFound) {
  mock_lines_.ExpectFileLines("/proc/self/cgroup",
                              {"7:net:/sys\n", "6:job:/sys\n"});

  EXPECT_ERROR_CODE(::util::error::NOT_FOUND,
                    factory_->DetectCgroupPath(0, CGROUP_MEMORY));
}

typedef CgroupFactoryTest GetSupportedHierarchiesTest;

TEST_F(GetSupportedHierarchiesTest, Success) {
  mock_lines_.ExpectFileLines(kProcCgroups,
                              {"cpu 1 1 1", "memory 1 1 1", "freezer 1 1 1"});

  StatusOr<vector<CgroupHierarchy>> statusor =
      factory_->GetSupportedHierarchies();
  ASSERT_OK(statusor);
  const vector<CgroupHierarchy> result = statusor.ValueOrDie();
  EXPECT_EQ(3, result.size());
  EXPECT_THAT(result, Contains(CGROUP_CPU));
  EXPECT_THAT(result, Contains(CGROUP_MEMORY));
  EXPECT_THAT(result, Contains(CGROUP_FREEZER));
}

TEST_F(GetSupportedHierarchiesTest, DisabledHierarchy) {
  mock_lines_.ExpectFileLines(kProcCgroups,
                              {"cpu 1 1 1", "memory 1 1 0", "freezer 1 1 1"});

  StatusOr<vector<CgroupHierarchy>> statusor =
      factory_->GetSupportedHierarchies();
  ASSERT_OK(statusor);
  const vector<CgroupHierarchy> result = statusor.ValueOrDie();
  EXPECT_EQ(2, result.size());
  EXPECT_THAT(result, Contains(CGROUP_CPU));
  EXPECT_THAT(result, Contains(CGROUP_FREEZER));
}

typedef CgroupFactoryTest PopulateMachineSpecTest;

TEST_F(PopulateMachineSpecTest, Success) {
  MachineSpec spec;
  Status status = factory_->PopulateMachineSpec(&spec);

  MachineSpec expected_spec;
  CgroupMount *mount1 = expected_spec.add_cgroup_mount();
  mount1->set_mount_path(kCpuMount);
  mount1->add_hierarchy(CGROUP_CPU);
  mount1->add_hierarchy(CGROUP_CPUACCT);
  CgroupMount *mount2 = expected_spec.add_cgroup_mount();
  mount2->set_mount_path(kMemoryMount);
  mount2->add_hierarchy(CGROUP_MEMORY);
  EXPECT_THAT(expected_spec, EqualsInitializedProto(spec));
}

}  // namespace lmctfy
}  // namespace containers
