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

//
// Tests for MntNsConfigurator class
//
#include "nscon/configurator/mnt_ns_configurator.h"

#include <sys/mount.h>
#include <memory>

#include "include/namespaces.pb.h"
#include "nscon/ns_util_mock.h"
#include "global_utils/mount_utils_test_util.h"
#include "util/errors_test_util.h"
#include "system_api/libc_fs_api_test_util.h"
#include "gtest/gtest.h"
#include "util/task/status.h"

using ::std::unique_ptr;
using ::std::vector;
using ::testing::_;
using ::testing::SetErrnoAndReturn;
using ::testing::StrEq;
using ::testing::NotNull;
using ::testing::Return;
using ::util::error::INTERNAL;
using ::util::error::INVALID_ARGUMENT;
using ::util::Status;

namespace containers {
namespace nscon {

class MntNsConfiguratorTest : public ::testing::Test {
 protected:
  typedef MntNsSpec_MountAction_Unmount Unmount;
  typedef MntNsSpec_MountAction_Mount Mount;

  void SetUp() override {
    mock_ns_util_.reset(new ::testing::StrictMock<MockNsUtil>());
    mnt_ns_config_.reset(new MntNsConfigurator(mock_ns_util_.get()));
  }

  Status CallDoUnmountAction(const Unmount &um) {
    return mnt_ns_config_->DoUnmountAction(um);
  }

  Status CallDoMountAction(const Mount &m) {
    return mnt_ns_config_->DoMountAction(m);
  }

  ::system_api::MockLibcFsApiOverride mock_libc_fs_api_;
  ::util::MockMountUtilsOverride mount_util_;
  unique_ptr<MockNsUtil> mock_ns_util_;
  unique_ptr<MntNsConfigurator> mnt_ns_config_;
};

TEST_F(MntNsConfiguratorTest, DoUnmountAction_NoPath) {
  Unmount um;
  EXPECT_OK(CallDoUnmountAction(um));
}

TEST_F(MntNsConfiguratorTest, DoUnmountAction_EmptyPath) {
  Unmount um;
  um.mutable_path();
  EXPECT_ERROR_CODE(INVALID_ARGUMENT, CallDoUnmountAction(um));
}

TEST_F(MntNsConfiguratorTest, DoUnmountAction_NoAbsolutePath) {
  Unmount um;
  um.set_path("../relative/path");
  EXPECT_ERROR_CODE(INVALID_ARGUMENT, CallDoUnmountAction(um));
}

TEST_F(MntNsConfiguratorTest, DoUnmountAction_SingleFile) {
  const char *kPathToUnmount = "/path/to/unmount";
  Unmount um;
  um.set_path(kPathToUnmount);
  EXPECT_CALL(mock_libc_fs_api_.Mock(), UMount(StrEq(kPathToUnmount)))
      .WillOnce(Return(0));
  EXPECT_OK(CallDoUnmountAction(um));
}

TEST_F(MntNsConfiguratorTest, DoUnmountAction_NonExistantPath) {
  const char *kPathToUnmount = "/this/path/has/no/mount";
  Unmount um;
  um.set_path(kPathToUnmount);
  EXPECT_CALL(mock_libc_fs_api_.Mock(), UMount(StrEq(kPathToUnmount)))
      .WillOnce(SetErrnoAndReturn(EINVAL, -1));
  EXPECT_OK(CallDoUnmountAction(um));
  EXPECT_CALL(mock_libc_fs_api_.Mock(), UMount(StrEq(kPathToUnmount)))
      .WillOnce(SetErrnoAndReturn(ENOENT, -1));
  EXPECT_OK(CallDoUnmountAction(um));
}

TEST_F(MntNsConfiguratorTest, DoUnmountAction_UmountFailure) {
  const char *kPathToUnmount = "/this/path/has/no/mount";
  Unmount um;
  um.set_path(kPathToUnmount);
  EXPECT_CALL(mock_libc_fs_api_.Mock(), UMount(StrEq(kPathToUnmount)))
      .WillOnce(SetErrnoAndReturn(EBUSY, -1));
  EXPECT_ERROR_CODE(INTERNAL, CallDoUnmountAction(um));
}

TEST_F(MntNsConfiguratorTest, DoUnmountAction_RecursiveUnmount) {
  const char *kPathToUnmount = "/path/to/unmount";
  Unmount um;
  um.set_path(kPathToUnmount);
  um.set_do_recursive(true);
  EXPECT_CALL(mount_util_.Mock(), UnmountRecursive(StrEq(kPathToUnmount)))
      .WillOnce(Return(Status::OK));
  EXPECT_OK(CallDoUnmountAction(um));
}

TEST_F(MntNsConfiguratorTest, DoMountAction_InvalidArgs) {
  Mount m;
  EXPECT_ERROR_CODE(INVALID_ARGUMENT, CallDoMountAction(m));
  m.set_source("");
  EXPECT_ERROR_CODE(INVALID_ARGUMENT, CallDoMountAction(m));
  m.set_target("");
  EXPECT_ERROR_CODE(INVALID_ARGUMENT, CallDoMountAction(m));
  m.set_target("../relative/path/");
  EXPECT_ERROR_CODE(INVALID_ARGUMENT, CallDoMountAction(m));
}

TEST_F(MntNsConfiguratorTest, DoMountAction_Success) {
  Mount m;
  m.set_source("proc");
  m.set_target("/proc/");
  m.set_fstype("proc");
  EXPECT_CALL(mock_libc_fs_api_.Mock(),
              Mount(StrEq("proc"), StrEq("/proc/"), StrEq("proc"), 0, _))
      .WillOnce(Return(0));
  EXPECT_OK(CallDoMountAction(m));
}

TEST_F(MntNsConfiguratorTest, DoMountAction_SuccessWithAllArgs) {
  Mount m;
  m.set_source("tmpfs");
  m.set_target("/tmpfs/");
  m.set_fstype("tmpfs");
  m.set_flags(MS_RDONLY);
  m.set_options("size=10M");
  EXPECT_CALL(mock_libc_fs_api_.Mock(),
              Mount(StrEq("tmpfs"), StrEq("/tmpfs/"), StrEq("tmpfs"), MS_RDONLY,
                    NotNull()))
      .WillOnce(Return(0));
  EXPECT_OK(CallDoMountAction(m));
}

TEST_F(MntNsConfiguratorTest, DoMountAction_MountFailure) {
  Mount m;
  m.set_source("tmpfs");
  m.set_target("/tmpfs/");
  m.set_fstype("tmpfs");
  m.set_flags(MS_RDONLY);
  m.set_options("size=10M");
  EXPECT_CALL(mock_libc_fs_api_.Mock(),
              Mount(StrEq("tmpfs"), StrEq("/tmpfs/"), StrEq("tmpfs"), MS_RDONLY,
                    NotNull()))
      .WillOnce(SetErrnoAndReturn(ENOMEM, -1));
  EXPECT_ERROR_CODE(INTERNAL, CallDoMountAction(m));
}

TEST_F(MntNsConfiguratorTest, SetupOutsideNamespace_Success) {
  NamespaceSpec spec;
  ASSERT_OK(mnt_ns_config_->SetupOutsideNamespace(spec, 1));
}

TEST_F(MntNsConfiguratorTest, SetupInsideNamespace_NoSpec) {
  NamespaceSpec spec;
  ASSERT_OK(mnt_ns_config_->SetupInsideNamespace(spec));
}

TEST_F(MntNsConfiguratorTest, SetupInsideNamespace_NoMntnsActions) {
  NamespaceSpec spec;
  spec.mutable_mnt();
  ASSERT_OK(mnt_ns_config_->SetupInsideNamespace(spec));
}

TEST_F(MntNsConfiguratorTest, SetupInsideNamespace_SuccessWithMultipleActions) {
  NamespaceSpec spec;
  MntNsSpec *mntns = spec.mutable_mnt();
  MntNsSpec_MountAction *mount_action = mntns->add_mount_action();
  // Add action for unmounting /sys
  Unmount *um = mount_action->mutable_unmount();
  um->set_path("/sys");
  um->set_do_recursive(true);
  // Add action for mounting /proc
  mount_action = mntns->add_mount_action();
  Mount *m = mount_action->mutable_mount();
  m->set_source("tmpfs");
  m->set_target("/mnt/tmpfs");
  m->set_fstype("tmpfs");

  EXPECT_CALL(mount_util_.Mock(), UnmountRecursive(StrEq("/sys")))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(mock_libc_fs_api_.Mock(),
              Mount(StrEq("tmpfs"), StrEq("/mnt/tmpfs"), StrEq("tmpfs"), 0, _))
      .WillOnce(Return(0));

  ASSERT_OK(mnt_ns_config_->SetupInsideNamespace(spec));
}

}  // namespace nscon
}  // namespace containers
