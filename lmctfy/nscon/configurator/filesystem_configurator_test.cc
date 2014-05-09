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
// Tests for FilesystemConfigurator class
//
#include "nscon/configurator/filesystem_configurator.h"

#include <fcntl.h>
#include <sys/mount.h>

#include <memory>

#include "file/base/path.h"
#include "include/namespaces.pb.h"
#include "nscon/ns_util_mock.h"
#include "global_utils/fs_utils_test_util.h"
#include "global_utils/mount_utils_test_util.h"
#include "global_utils/time_utils_test_util.h"
#include "util/errors_test_util.h"
#include "util/file_lines_test_util.h"
#include "util/proc_mounts.h"
#include "system_api/libc_fs_api_test_util.h"
#include "strings/split.h"
#include "strings/substitute.h"
#include "gtest/gtest.h"
#include "util/task/status.h"

using ::containers::Mounts;
using ::util::FileLinesTestUtil;
using ::util::MountUtils;
using ::std::unique_ptr;
using ::strings::Substitute;
using ::testing::ContainerEq;
using ::testing::Eq;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::SetErrnoAndReturn;
using ::testing::StrEq;
using ::testing::_;
using ::util::error::INTERNAL;
using ::util::error::INVALID_ARGUMENT;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace nscon {

static const ::util::Microseconds kTime(1392567140);
static const char kProcMountsPath[] = "/proc/mounts";
static const vector<string> kProcMounts_SystemRoot = {
  "rootfs / rootfs rw 0 0",
  "/dev/root / ext4 rw,relatime 0 0",
};
static const vector<string> kProcMounts_ProcSys = {
  "proc /proc proc rw,nosuid,nodev,noexec 0 0",
  "sysfs /sys sysfs rw,nosuid,nodev,noexec 0 0",
};
static const vector<string> kProcMounts_Stdfs = {
  "configfs /sys/kernel/config configfs rw,nosuid,nodev,noexec 0 0",
  "debugfs /sys/kernel/debug debugfs rw,nosuid,nodev,noexec 0 0",
  "varrun /var/run tmpfs rw,nosuid,noexec,size=256k,mode=755 0 0",
  "varlock /var/lock tmpfs rw,nosuid,nodev,noexec,size=64k 0 0",
  "tmpfs /mnt tmpfs rw,nosuid,nodev,noexec,size=12k,mode=755 0 0",
  "tmpfs /dev/shm tmpfs rw,nosuid,nodev,size=64k,mode=755 0 0",
  "devpts /dev/pts devpts rw,nosuid,noexec,gid=5,mode=620 0 0",
  "none /proc/partitions tmpfs ro 0 0",
  "/dev/hda3 /export/hda3 ext4 rw 0 0",
  "/dev/hdc3 /export/hdc3 ext4 rw 0 0",
};
static const vector<string> kProcMounts_CustomRootfs = {
  "tmpfs /export/tmpfs tmpfs rw 0 0",
  "tmpfs /export/tmpfs/root tmpfs rw 0 0",
};

class FilesystemConfiguratorTest : public ::testing::Test {
 public:
  void SetUp() override {
    mock_file_lines_.reset(new FileLinesTestUtil(&mock_libc_fs_api_));
    mock_ns_util_.reset(new ::testing::StrictMock<MockNsUtil>());
    fs_config_.reset(new FilesystemConfigurator(mock_ns_util_.get()));

    // Setup procfs contents.
    proc_mount_contents_.clear();
    proc_mount_contents_ = kProcMounts_SystemRoot;
    proc_mount_contents_.insert(proc_mount_contents_.end(),
                                kProcMounts_ProcSys.begin(),
                                kProcMounts_ProcSys.end());
    proc_mount_contents_.insert(proc_mount_contents_.end(),
                                kProcMounts_Stdfs.begin(),
                                kProcMounts_Stdfs.end());
    proc_mount_contents_.insert(proc_mount_contents_.end(),
                                kProcMounts_CustomRootfs.begin(),
                                kProcMounts_CustomRootfs.end());

    // By default, any unmount will return failure.
    EXPECT_CALL(mock_libc_fs_api_.Mock(), UMount(_))
        .WillRepeatedly(SetErrnoAndReturn(EPERM, -1));
    EXPECT_CALL(mock_libc_fs_api_.Mock(), UMount2(_, _))
        .WillRepeatedly(SetErrnoAndReturn(EPERM, -1));
  }

  Status CallPrepareFilesystem(const string &rootfs_path) const {
    return fs_config_->PrepareFilesystem(whitelisted_mounts_, rootfs_path);
  }

  Status CallSetupChroot(const string &rootfs_path) const {
    return fs_config_->SetupChroot(rootfs_path);
  }

  Status CallSetupPivotRoot(const string &rootfs_path) const {
    return fs_config_->SetupPivotRoot(rootfs_path);
  }

  Status CallSetupProcfs(const string &procfs_path) const {
    return fs_config_->SetupProcfs(procfs_path);
  }

  Status CallSetupSysfs(const string &sysfs_path) const {
    return fs_config_->SetupSysfs(sysfs_path);
  }

  Status CallSetupDevpts() const {
    return fs_config_->SetupDevpts();
  }

  Status CallSetupExternalMounts(const Mounts &mounts,
                                 const string &rootfs_path) const {
    const auto whitelisted_mounts =
        RETURN_IF_ERROR(fs_config_->SetupExternalMounts(mounts, rootfs_path));
    EXPECT_THAT(whitelisted_mounts, ContainerEq(whitelisted_mounts_));
    return Status::OK;
  }

  void ExpectUnmounts(const vector<string> &proc_mount_lines) {
    for (const string &line : proc_mount_lines) {
      vector<string> fields = strings::Split(line, " ");
      EXPECT_CALL(mock_libc_fs_api_.Mock(), UMount(StrEq(fields[1])));
    }
  }

  void ExpectPathExists(const string &path) {
    EXPECT_CALL(mock_fs_utils_.Mock(), FileExists(StrEq(path)))
        .WillOnce(Return(true));
  }

  void ExpectPathNotExists(const string &path) {
    EXPECT_CALL(mock_fs_utils_.Mock(), FileExists(StrEq(path)))
        .WillOnce(Return(false));
  }

  void ExpectDevptsSetupCalls() {
    ExpectPathExists("/dev/pts");
    ExpectPathExists("/dev/ptmx");

    EXPECT_CALL(mock_libc_fs_api_.Mock(),
                Mount(StrEq("devpts"), StrEq("/dev/pts"), StrEq("devpts"),
                      (MS_NODEV | MS_NOEXEC | MS_NOSUID | MS_RELATIME), _))
        .WillOnce(Return(0));
    ExpectPathNotExists("/dev/pts/ptmx");
  }

  void ExpectBindMount(const string &source, const string &target,
                       bool read_only, bool private_mount, Status result) {
    ExpectPathExists(source);
    ExpectPathExists(target);
    ::std::set<MountUtils::BindMountOpts> opts({MountUtils::RECURSIVE});
    if (read_only) {
      opts.insert(MountUtils::READONLY);
    }
    if (private_mount) {
      opts.insert(MountUtils::PRIVATE);
    }
    EXPECT_CALL(mock_mount_utils_.Mock(),
                BindMount(StrEq(source), StrEq(target), ContainerEq(opts)))
        .WillOnce(Return(result));
    whitelisted_mounts_.insert(target);
  }

  void AddMount(Mounts *mounts,
                const string &source,
                const string &target,
                bool read_only,
                bool private_mount) {
    auto mount = mounts->add_mount();
    if (!source.empty()) {
      mount->set_source(source);
    }
    if (!target.empty()) {
      mount->set_target(target);
    }
    mount->set_read_only(read_only);
    mount->set_private_(private_mount);
  }

 protected:
  const pid_t kPid = 9999;
  const char *kFsRoot = "/";
  const char *kDefaultProcfsPath = "/proc/";
  const char *kDefaultSysfsPath = "/sys/";
  const char *kCustomRootfsPath = "/export/tmpfs/root";

  ::std::set<string> whitelisted_mounts_;
  vector<string> proc_mount_contents_;
  system_api::MockLibcFsApiOverride mock_libc_fs_api_;
  util::MockFsUtilsOverride mock_fs_utils_;
  util::MockMountUtilsOverride mock_mount_utils_;
  util::MockTimeUtilsOverride mock_time_utils_;
  unique_ptr<FileLinesTestUtil> mock_file_lines_;
  unique_ptr<MockNsUtil> mock_ns_util_;
  unique_ptr<FilesystemConfigurator> fs_config_;
};

TEST_F(FilesystemConfiguratorTest, SetupOutsideNamespace_NoSpec) {
  NamespaceSpec spec;
  ASSERT_OK(fs_config_->SetupOutsideNamespace(spec, kPid));
  spec.mutable_fs();
  ASSERT_OK(fs_config_->SetupOutsideNamespace(spec, kPid));
}

TEST_F(FilesystemConfiguratorTest, SetupOutsideNamespace_FsSpec) {
  NamespaceSpec spec;
  FilesystemSpec *fs = spec.mutable_fs();
  fs->set_rootfs_path("/root/fs/path");
  ASSERT_OK(fs_config_->SetupOutsideNamespace(spec, kPid));
}

typedef FilesystemConfiguratorTest SetupExternalMountsTest;

TEST_F(SetupExternalMountsTest, Success_NoMounts) {
  Mounts mounts;
  EXPECT_OK(CallSetupExternalMounts(mounts, kFsRoot));
}

TEST_F(SetupExternalMountsTest, Failure_InvalidMounts) {
  Mounts mounts;
  mounts.add_mount()->set_source("x");
  EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT,
                    CallSetupExternalMounts(mounts, kFsRoot));
  mounts.Clear();
  mounts.add_mount()->set_target("x");
  EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT,
                    CallSetupExternalMounts(mounts, kFsRoot));
  mounts.Clear();
  mounts.add_mount()->set_target("");
  EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT,
                    CallSetupExternalMounts(mounts, kFsRoot));
  mounts.Clear();
  mounts.add_mount()->set_source("");
  EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT,
                    CallSetupExternalMounts(mounts, kFsRoot));
}

TEST_F(SetupExternalMountsTest, Success) {
  Mounts mounts;
  AddMount(&mounts, "/a", "/b", true, false);
  ExpectBindMount("/a", ::file::JoinPath(kCustomRootfsPath, "/b"), true, false,
                  Status::OK);
  AddMount(&mounts, "/c", "/d", false, true);
  ExpectBindMount("/c", ::file::JoinPath(kCustomRootfsPath, "/d"), false, true,
                  Status::OK);
  AddMount(&mounts, "/e", "/f", true, true);
  ExpectBindMount("/e", ::file::JoinPath(kCustomRootfsPath, "/f"), true, true,
                  Status::OK);

  EXPECT_OK(CallSetupExternalMounts(mounts, kCustomRootfsPath));
}

TEST_F(SetupExternalMountsTest, Failure_SourceNotExist) {
  Mounts mounts;
  AddMount(&mounts, "/a", "/b", true, false);
  ExpectBindMount("/a", ::file::JoinPath(kCustomRootfsPath, "/b"), true, false,
                  Status::OK);
  AddMount(&mounts, "/c", "/d", false, true);
  ExpectPathNotExists("/c");
  AddMount(&mounts, "/e", "/f", true, true);

  EXPECT_ERROR_CODE(INTERNAL, CallSetupExternalMounts(mounts,
                                                      kCustomRootfsPath));
}

TEST_F(SetupExternalMountsTest, Failure_TargetNotExist) {
  Mounts mounts;
  AddMount(&mounts, "/a", "/b", true, false);
  ExpectBindMount("/a", ::file::JoinPath(kCustomRootfsPath, "/b"), true, false,
                  Status::OK);
  AddMount(&mounts, "/c", "/d", false, true);
  ExpectPathExists("/c");
  ExpectPathNotExists(::file::JoinPath(kCustomRootfsPath, "/d"));
  AddMount(&mounts, "/e", "/f", true, true);

  EXPECT_ERROR_CODE(INTERNAL, CallSetupExternalMounts(mounts,
                                                      kCustomRootfsPath));
}

TEST_F(SetupExternalMountsTest, Failure_BindMountError) {
  Mounts mounts;
  AddMount(&mounts, "/a", "/b", true, false);
  ExpectBindMount("/a", "/b", true, false, Status::OK);
  AddMount(&mounts, "/c", "/d", false, true);
  ExpectBindMount("/c", "/d", false, true, Status(INTERNAL, "blah"));
  AddMount(&mounts, "/e", "/f", true, true);

  EXPECT_ERROR_CODE(INTERNAL, CallSetupExternalMounts(mounts, kFsRoot));
}

typedef FilesystemConfiguratorTest PrepareFilesystemTest;

TEST_F(PrepareFilesystemTest, DefaultRootfs) {
  mock_file_lines_->ExpectFileLines(kProcMountsPath,
                                    proc_mount_contents_);
  EXPECT_CALL(mock_libc_fs_api_.Mock(), ChDir(StrEq("/"))).WillOnce(Return(0));
  ExpectUnmounts(kProcMounts_ProcSys);
  ExpectUnmounts(kProcMounts_Stdfs);
  ExpectUnmounts(kProcMounts_CustomRootfs);

  ASSERT_OK(CallPrepareFilesystem("/"));
}

TEST_F(PrepareFilesystemTest, Success_CustomRootfsWithWhitelistedMounts) {
  const vector<string> kMountLines = {
    "/x /root/y ext4 rw,nosuid,nodev,noexec 0 0",
    "/a /root/b ext4 rw,nosuid,nodev,noexec 0 0",
    "proc /proc proc rw,nosuid,nodev,noexec 0 0",
    "sysfs /sys sysfs rw,nosuid,nodev,noexec 0 0",
  };
  mock_file_lines_->ExpectFileLines(kProcMountsPath,
                                    kMountLines);
  EXPECT_CALL(mock_libc_fs_api_.Mock(),
              ChDir(StrEq("/root"))).WillOnce(Return(0));
  ExpectUnmounts(kProcMounts_ProcSys);
  whitelisted_mounts_.insert("/root/y");
  whitelisted_mounts_.insert("/root/b");

  ASSERT_OK(CallPrepareFilesystem("/root"));
}

TEST_F(PrepareFilesystemTest, Success_DefaultRootfsWithWhitelistedMounts) {
  const vector<string> kMountLines = {
    "/a /x/y/z ext4 rw,nosuid,nodev,noexec 0 0",
    "/b /x ext4 rw,nosuid,nodev,noexec 0 0",
    "/c /x/y ext4 rw,nosuid,nodev,noexec 0 0",
    "proc /proc proc rw,nosuid,nodev,noexec 0 0",
    "sysfs /sys sysfs rw,nosuid,nodev,noexec 0 0",
  };
  mock_file_lines_->ExpectFileLines(kProcMountsPath,
                                    kMountLines);
  EXPECT_CALL(mock_libc_fs_api_.Mock(),
              ChDir(StrEq("/"))).WillOnce(Return(0));
  ExpectUnmounts(kProcMounts_ProcSys);
  whitelisted_mounts_.insert("/x/y");

  ASSERT_OK(CallPrepareFilesystem("/"));
}

TEST_F(PrepareFilesystemTest, CustomRootfs) {
  mock_file_lines_->ExpectFileLines(kProcMountsPath,
                                    proc_mount_contents_);

  EXPECT_CALL(mock_libc_fs_api_.Mock(), ChDir(StrEq(kCustomRootfsPath)))
      .WillOnce(Return(0));

  // Unmounts everything except the mounts along the pivot-root path.
  ExpectUnmounts(kProcMounts_ProcSys);
  ExpectUnmounts(kProcMounts_Stdfs);

  ASSERT_OK(CallPrepareFilesystem(kCustomRootfsPath));
}

TEST_F(PrepareFilesystemTest, CustomRootfs_UMountFailure) {
  mock_file_lines_->ExpectFileLines(kProcMountsPath,
                                    proc_mount_contents_);

  EXPECT_CALL(mock_libc_fs_api_.Mock(), ChDir(StrEq(kCustomRootfsPath)))
      .WillOnce(Return(0));

  // By default, all unmounts fail. So no need to set any expectations here.
  EXPECT_ERROR_CODE(INTERNAL, CallPrepareFilesystem(kCustomRootfsPath));
}

typedef FilesystemConfiguratorTest SetupChrootTest;

TEST_F(SetupChrootTest, DefaultRootfs) {
  // ChDir
  EXPECT_CALL(mock_libc_fs_api_.Mock(), ChDir(StrEq("/")))
      .WillOnce(Return(0));
  ASSERT_OK(CallSetupChroot("/"));
}

TEST_F(SetupChrootTest, CustomRootfs) {
  EXPECT_CALL(mock_libc_fs_api_.Mock(), ChDir(StrEq(kCustomRootfsPath)))
      .WillOnce(Return(0));
  EXPECT_CALL(mock_libc_fs_api_.Mock(), ChRoot(StrEq(kCustomRootfsPath)))
      .WillOnce(Return(0));

  ASSERT_OK(CallSetupChroot(kCustomRootfsPath));
}

TEST_F(SetupChrootTest, ChrootFailure) {
  EXPECT_CALL(mock_libc_fs_api_.Mock(), ChDir(StrEq(kCustomRootfsPath)))
      .WillOnce(Return(0));
  EXPECT_CALL(mock_libc_fs_api_.Mock(), ChRoot(StrEq(kCustomRootfsPath)))
      .WillOnce(SetErrnoAndReturn(EPERM, -1));

  EXPECT_ERROR_CODE(INTERNAL, CallSetupChroot(kCustomRootfsPath));
}

typedef FilesystemConfiguratorTest SetupPivotRootTest;

TEST_F(SetupPivotRootTest, DefaultRootfs) {
  // ChDir
  EXPECT_CALL(mock_libc_fs_api_.Mock(), ChDir(StrEq("/")))
      .WillOnce(Return(0));
  ASSERT_OK(CallSetupPivotRoot("/"));
}

TEST_F(SetupPivotRootTest, CustomRootfs) {
  // ChDir
  EXPECT_CALL(mock_libc_fs_api_.Mock(), ChDir(StrEq(kCustomRootfsPath)))
      .WillOnce(Return(0));
  EXPECT_CALL(mock_time_utils_.Mock(), MicrosecondsSinceEpoch())
      .WillOnce(Return(kTime));

  const string kOldRoot = Substitute("nscon.old_root.$0", kTime.value());
  EXPECT_CALL(mock_libc_fs_api_.Mock(), MkDir(StrEq(kOldRoot), 0700))
      .WillOnce(Return(0));
  EXPECT_CALL(mock_libc_fs_api_.Mock(), PivotRoot(StrEq("."), StrEq(kOldRoot)))
      .WillOnce(Return(0));
  EXPECT_CALL(mock_libc_fs_api_.Mock(), ChDir(StrEq("/"))).WillOnce(Return(0));
  EXPECT_CALL(mock_libc_fs_api_.Mock(), UMount2(StrEq(kOldRoot), MNT_DETACH))
      .WillOnce(Return(0));
  EXPECT_CALL(mock_libc_fs_api_.Mock(), RmDir(StrEq(kOldRoot)))
      .WillOnce(Return(0));

  ASSERT_OK(CallSetupPivotRoot(kCustomRootfsPath));
}

TEST_F(SetupPivotRootTest, ChDirFailure) {
  // ChDir
  EXPECT_CALL(mock_libc_fs_api_.Mock(), ChDir(StrEq(kCustomRootfsPath)))
      .WillOnce(SetErrnoAndReturn(EACCES, -1));

  EXPECT_ERROR_CODE(INTERNAL, CallSetupPivotRoot(kCustomRootfsPath));
}

TEST_F(SetupPivotRootTest, MkDirFailure) {
  // ChDir
  EXPECT_CALL(mock_libc_fs_api_.Mock(), ChDir(StrEq(kCustomRootfsPath)))
      .WillOnce(Return(0));
  EXPECT_CALL(mock_time_utils_.Mock(), MicrosecondsSinceEpoch())
      .WillOnce(Return(kTime));

  const string kOldRoot = Substitute("nscon.old_root.$0", kTime.value());
  EXPECT_CALL(mock_libc_fs_api_.Mock(), MkDir(StrEq(kOldRoot), 0700))
      .WillOnce(SetErrnoAndReturn(EACCES, -1));

  EXPECT_ERROR_CODE(INTERNAL, CallSetupPivotRoot(kCustomRootfsPath));
}

TEST_F(SetupPivotRootTest, PivotRootFailure) {
  // ChDir
  EXPECT_CALL(mock_libc_fs_api_.Mock(), ChDir(StrEq(kCustomRootfsPath)))
      .WillOnce(Return(0));
  EXPECT_CALL(mock_time_utils_.Mock(), MicrosecondsSinceEpoch())
      .WillOnce(Return(kTime));

  const string kOldRoot = Substitute("nscon.old_root.$0", kTime.value());
  EXPECT_CALL(mock_libc_fs_api_.Mock(), MkDir(StrEq(kOldRoot), 0700))
      .WillOnce(Return(0));

  EXPECT_CALL(mock_libc_fs_api_.Mock(), PivotRoot(StrEq("."), StrEq(kOldRoot)))
      .WillOnce(SetErrnoAndReturn(EBUSY, -1));

  EXPECT_CALL(mock_libc_fs_api_.Mock(), UMount2(StrEq(kOldRoot), MNT_DETACH))
      .WillOnce(Return(0));
  EXPECT_CALL(mock_libc_fs_api_.Mock(), RmDir(StrEq(kOldRoot)))
      .WillOnce(Return(0));

  EXPECT_ERROR_CODE(INTERNAL, CallSetupPivotRoot(kCustomRootfsPath));
}

TEST_F(SetupPivotRootTest, ChDirToRootFailure) {
  // ChDir
  EXPECT_CALL(mock_libc_fs_api_.Mock(), ChDir(StrEq(kCustomRootfsPath)))
      .WillOnce(Return(0));
  EXPECT_CALL(mock_time_utils_.Mock(), MicrosecondsSinceEpoch())
      .WillOnce(Return(kTime));

  const string kOldRoot = Substitute("nscon.old_root.$0", kTime.value());
  EXPECT_CALL(mock_libc_fs_api_.Mock(), MkDir(StrEq(kOldRoot), 0700))
      .WillOnce(Return(0));

  EXPECT_CALL(mock_libc_fs_api_.Mock(), PivotRoot(StrEq("."), StrEq(kOldRoot)))
      .WillOnce(Return(0));
  EXPECT_CALL(mock_libc_fs_api_.Mock(), ChDir(StrEq("/")))
      .WillOnce(SetErrnoAndReturn(EFAULT, -1));
  EXPECT_CALL(mock_libc_fs_api_.Mock(), UMount2(StrEq(kOldRoot), MNT_DETACH))
      .WillOnce(Return(0));
  EXPECT_CALL(mock_libc_fs_api_.Mock(), RmDir(StrEq(kOldRoot)))
      .WillOnce(Return(0));

  EXPECT_ERROR_CODE(INTERNAL, CallSetupPivotRoot(kCustomRootfsPath));
}

TEST_F(SetupPivotRootTest, OldrootUmountFailure) {
  // ChDir
  EXPECT_CALL(mock_libc_fs_api_.Mock(), ChDir(StrEq(kCustomRootfsPath)))
      .WillOnce(Return(0));
  EXPECT_CALL(mock_time_utils_.Mock(), MicrosecondsSinceEpoch())
      .WillOnce(Return(kTime));

  const string kOldRoot = Substitute("nscon.old_root.$0", kTime.value());
  EXPECT_CALL(mock_libc_fs_api_.Mock(), MkDir(StrEq(kOldRoot), 0700))
      .WillOnce(Return(0));
  EXPECT_CALL(mock_libc_fs_api_.Mock(), PivotRoot(StrEq("."), StrEq(kOldRoot)))
      .WillOnce(Return(0));
  EXPECT_CALL(mock_libc_fs_api_.Mock(), ChDir(StrEq("/"))).WillOnce(Return(0));

  EXPECT_CALL(mock_libc_fs_api_.Mock(), UMount2(StrEq(kOldRoot), MNT_DETACH))
      .Times(2)
      .WillRepeatedly(SetErrnoAndReturn(EBUSY, -1));
  EXPECT_CALL(mock_libc_fs_api_.Mock(), RmDir(StrEq(kOldRoot)))
      .WillOnce(Return(0));

  EXPECT_ERROR_CODE(INTERNAL, CallSetupPivotRoot(kCustomRootfsPath));
}

TEST_F(SetupPivotRootTest, OldrootRmdirFailure) {
  // ChDir
  EXPECT_CALL(mock_libc_fs_api_.Mock(), ChDir(StrEq(kCustomRootfsPath)))
      .WillOnce(Return(0));
  EXPECT_CALL(mock_time_utils_.Mock(), MicrosecondsSinceEpoch())
      .WillOnce(Return(kTime));

  const string kOldRoot = Substitute("nscon.old_root.$0", kTime.value());
  EXPECT_CALL(mock_libc_fs_api_.Mock(), MkDir(StrEq(kOldRoot), 0700))
      .WillOnce(Return(0));
  EXPECT_CALL(mock_libc_fs_api_.Mock(), PivotRoot(StrEq("."), StrEq(kOldRoot)))
      .WillOnce(Return(0));
  EXPECT_CALL(mock_libc_fs_api_.Mock(), ChDir(StrEq("/"))).WillOnce(Return(0));
  EXPECT_CALL(mock_libc_fs_api_.Mock(), UMount2(StrEq(kOldRoot), MNT_DETACH))
      .WillOnce(Return(0));
  EXPECT_CALL(mock_libc_fs_api_.Mock(), RmDir(StrEq(kOldRoot)))
      .WillOnce(SetErrnoAndReturn(EBUSY, -1));

  EXPECT_ERROR_CODE(INTERNAL, CallSetupPivotRoot(kCustomRootfsPath));
}

typedef FilesystemConfiguratorTest SetupProcfsTest;

TEST_F(SetupProcfsTest, DefaultRootfs) {
  EXPECT_CALL(mock_libc_fs_api_.Mock(),
              Mount(StrEq("proc"), StrEq("/proc/"), StrEq("proc"),
                    (MS_NODEV | MS_NOEXEC | MS_NOSUID | MS_RELATIME), nullptr))
      .WillOnce(Return(0));
  ASSERT_OK(CallSetupProcfs(kDefaultProcfsPath));
}

TEST_F(SetupProcfsTest, CustomProcfs) {
  const char kCustomProcfsPath[] = "/custom/root/procfs";

  EXPECT_CALL(mock_libc_fs_api_.Mock(),
              Mount(StrEq("proc"), StrEq("/custom/root/procfs"), StrEq("proc"),
                    (MS_NODEV | MS_NOEXEC | MS_NOSUID | MS_RELATIME), nullptr))
      .WillOnce(Return(0));
  ASSERT_OK(CallSetupProcfs(kCustomProcfsPath));
}

TEST_F(SetupProcfsTest, MountFailure) {
  const char kCustomProcfsPath[] = "/custom/root/procfs";

  EXPECT_CALL(mock_libc_fs_api_.Mock(),
              Mount(StrEq("proc"), StrEq("/custom/root/procfs"), StrEq("proc"),
                    (MS_NODEV | MS_NOEXEC | MS_NOSUID | MS_RELATIME), nullptr))
      .WillOnce(SetErrnoAndReturn(EBUSY, -1));
  EXPECT_ERROR_CODE(INTERNAL, CallSetupProcfs(kCustomProcfsPath));
}


typedef FilesystemConfiguratorTest SetupSysfsTest;

TEST_F(SetupSysfsTest, DefaultRootfs) {
  EXPECT_CALL(mock_libc_fs_api_.Mock(),
              Mount(StrEq("sysfs"), StrEq("/sys/"), StrEq("sysfs"),
                    (MS_NODEV | MS_NOEXEC | MS_NOSUID | MS_RELATIME), nullptr))
      .WillOnce(Return(0));
  ASSERT_OK(CallSetupSysfs(kDefaultSysfsPath));
}

TEST_F(SetupSysfsTest, CustomSysfs) {
  const char kCustomSysfsPath[] = "/custom/root/sysfs";

  EXPECT_CALL(mock_libc_fs_api_.Mock(),
              Mount(StrEq("sysfs"), StrEq("/custom/root/sysfs"), StrEq("sysfs"),
                    (MS_NODEV | MS_NOEXEC | MS_NOSUID | MS_RELATIME), nullptr))
      .WillOnce(Return(0));
  ASSERT_OK(CallSetupSysfs(kCustomSysfsPath));
}

TEST_F(SetupSysfsTest, MountFailure) {
  const char kCustomSysfsPath[] = "/custom/root/sysfs";

  EXPECT_CALL(mock_libc_fs_api_.Mock(),
              Mount(StrEq("sysfs"), StrEq("/custom/root/sysfs"), StrEq("sysfs"),
                    (MS_NODEV | MS_NOEXEC | MS_NOSUID | MS_RELATIME), nullptr))
      .WillOnce(SetErrnoAndReturn(EBUSY, -1));
  EXPECT_ERROR_CODE(INTERNAL, CallSetupSysfs(kCustomSysfsPath));
}

typedef FilesystemConfiguratorTest SetupDevptsTest;

TEST_F(SetupDevptsTest, SuccessWithoutDevptsNamespaceDevptmxExists) {
  ExpectDevptsSetupCalls();

  EXPECT_OK(CallSetupDevpts());
}

TEST_F(SetupDevptsTest, Failure_WithoutDevptsNamespace_DevptmxNotExists) {
  ExpectPathExists("/dev/pts");
  ExpectPathNotExists("/dev/ptmx");

  EXPECT_ERROR_CODE(INTERNAL, CallSetupDevpts());
}

TEST_F(SetupDevptsTest, Failure_DevptsNotExists) {
  ExpectPathNotExists("/dev/pts");

  EXPECT_ERROR_CODE(INTERNAL, CallSetupDevpts());
}

TEST_F(SetupDevptsTest, Failure_DevptsMountError) {
  ExpectPathExists("/dev/pts");
  ExpectPathExists("/dev/ptmx");
  EXPECT_CALL(mock_libc_fs_api_.Mock(),
              Mount(StrEq("devpts"), StrEq("/dev/pts"), StrEq("devpts"),
                    (MS_NODEV | MS_NOEXEC | MS_NOSUID | MS_RELATIME), _))
      .WillOnce(SetErrnoAndReturn(EPERM, -1));

  EXPECT_ERROR_CODE(INTERNAL, CallSetupDevpts());
}

TEST_F(SetupDevptsTest, Failure_DevptsPtmxFileExistsError) {
  ExpectPathExists("/dev/pts");
  ExpectPathExists("/dev/ptmx");
  EXPECT_CALL(mock_libc_fs_api_.Mock(),
              Mount(StrEq("devpts"), StrEq("/dev/pts"), StrEq("devpts"),
                    (MS_NODEV | MS_NOEXEC | MS_NOSUID | MS_RELATIME), _))
      .WillOnce(Return(0));

  EXPECT_CALL(mock_fs_utils_.Mock(), FileExists(StrEq("/dev/pts/ptmx")))
      .WillOnce(Return(Status(INTERNAL, "blah")));

  EXPECT_ERROR_CODE(INTERNAL, CallSetupDevpts());
}

TEST_F(SetupDevptsTest, Success_WithDevptsNamespace) {
  ExpectPathExists("/dev/pts");
  ExpectPathExists("/dev/ptmx");
  ExpectPathExists("/dev/pts/ptmx");

  EXPECT_CALL(mock_libc_fs_api_.Mock(),
              Mount(StrEq("devpts"), StrEq("/dev/pts"), StrEq("devpts"),
                    (MS_NODEV | MS_NOEXEC | MS_NOSUID | MS_RELATIME), _))
      .WillOnce(Return(0));
  ::std::set<::util::MountUtils::BindMountOpts> opts;
  EXPECT_CALL(mock_mount_utils_.Mock(), BindMount(StrEq("/dev/pts/ptmx"),
                                                  StrEq("/dev/ptmx"),
                                                  ::testing::ContainerEq(opts)))
      .WillOnce(Return(Status::OK));

  EXPECT_OK(CallSetupDevpts());
}

TEST_F(SetupDevptsTest, FailureWithDevptsNamespaceBindMountError) {
  ExpectPathExists("/dev/pts");
  ExpectPathExists("/dev/ptmx");
  ExpectPathExists("/dev/pts/ptmx");

  EXPECT_CALL(mock_libc_fs_api_.Mock(),
              Mount(StrEq("devpts"), StrEq("/dev/pts"), StrEq("devpts"),
                    (MS_NODEV | MS_NOEXEC | MS_NOSUID | MS_RELATIME), _))
      .WillOnce(Return(0));

  ::std::set<::util::MountUtils::BindMountOpts> opts;
  EXPECT_CALL(mock_mount_utils_.Mock(), BindMount(StrEq("/dev/pts/ptmx"),
                                                  StrEq("/dev/ptmx"),
                                                  ::testing::ContainerEq(opts)))
      .WillOnce(Return(Status(INTERNAL, "blah")));

  EXPECT_ERROR_CODE(INTERNAL, CallSetupDevpts());
}

typedef FilesystemConfiguratorTest SetupInsideNamespace;

TEST_F(SetupInsideNamespace, NoFsSpec) {
  NamespaceSpec spec;

  // Expect default setup.
  mock_file_lines_->ExpectFileLines(kProcMountsPath,
                                    proc_mount_contents_);
  EXPECT_CALL(mock_libc_fs_api_.Mock(), ChDir(StrEq("/")))
      .WillRepeatedly(Return(0));
  ExpectUnmounts(kProcMounts_ProcSys);
  ExpectUnmounts(kProcMounts_Stdfs);
  ExpectUnmounts(kProcMounts_CustomRootfs);

  EXPECT_CALL(mock_libc_fs_api_.Mock(), ChDir(StrEq("/")))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(mock_libc_fs_api_.Mock(),
              Mount(StrEq("proc"), StrEq("/proc/"), StrEq("proc"),
                    (MS_NODEV | MS_NOEXEC | MS_NOSUID | MS_RELATIME), nullptr))
      .WillOnce(Return(0));
  EXPECT_CALL(mock_libc_fs_api_.Mock(),
              Mount(StrEq("sysfs"), StrEq("/sys/"), StrEq("sysfs"),
                    (MS_NODEV | MS_NOEXEC | MS_NOSUID | MS_RELATIME), nullptr))
      .WillOnce(Return(0));
  ExpectDevptsSetupCalls();

  ASSERT_OK(fs_config_->SetupInsideNamespace(spec));
}

TEST_F(SetupInsideNamespace, Success_DevptsSetupError_NoConsole) {
  NamespaceSpec spec;

  // Expect default setup.
  mock_file_lines_->ExpectFileLines(kProcMountsPath,
                                    proc_mount_contents_);
  EXPECT_CALL(mock_libc_fs_api_.Mock(), ChDir(StrEq("/")))
      .WillRepeatedly(Return(0));
  ExpectUnmounts(kProcMounts_ProcSys);
  ExpectUnmounts(kProcMounts_Stdfs);
  ExpectUnmounts(kProcMounts_CustomRootfs);

  EXPECT_CALL(mock_libc_fs_api_.Mock(), ChDir(StrEq("/")))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(mock_libc_fs_api_.Mock(),
              Mount(StrEq("proc"), StrEq("/proc/"), StrEq("proc"),
                    (MS_NODEV | MS_NOEXEC | MS_NOSUID | MS_RELATIME), nullptr))
      .WillOnce(Return(0));
  EXPECT_CALL(mock_libc_fs_api_.Mock(),
              Mount(StrEq("sysfs"), StrEq("/sys/"), StrEq("sysfs"),
                    (MS_NODEV | MS_NOEXEC | MS_NOSUID | MS_RELATIME), nullptr))
      .WillOnce(Return(0));
  // This will fail dev pts setup.
  ExpectPathNotExists("/dev/pts");

  ASSERT_OK(fs_config_->SetupInsideNamespace(spec));
}

TEST_F(SetupInsideNamespace, Failure_DevptsSetupError_WithConsole) {
  NamespaceSpec spec;
  spec.mutable_run_spec()->mutable_console()->set_slave_pty("1");

  // Expect default setup.
  mock_file_lines_->ExpectFileLines(kProcMountsPath,
                                    proc_mount_contents_);
  EXPECT_CALL(mock_libc_fs_api_.Mock(), ChDir(StrEq("/")))
      .WillRepeatedly(Return(0));
  ExpectUnmounts(kProcMounts_ProcSys);
  ExpectUnmounts(kProcMounts_Stdfs);
  ExpectUnmounts(kProcMounts_CustomRootfs);

  EXPECT_CALL(mock_libc_fs_api_.Mock(), ChDir(StrEq("/")))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(mock_libc_fs_api_.Mock(),
              Mount(StrEq("proc"), StrEq("/proc/"), StrEq("proc"),
                    (MS_NODEV | MS_NOEXEC | MS_NOSUID | MS_RELATIME), nullptr))
      .WillOnce(Return(0));
  EXPECT_CALL(mock_libc_fs_api_.Mock(),
              Mount(StrEq("sysfs"), StrEq("/sys/"), StrEq("sysfs"),
                    (MS_NODEV | MS_NOEXEC | MS_NOSUID | MS_RELATIME), nullptr))
      .WillOnce(Return(0));
  // This will fail dev pts setup.
  ExpectPathNotExists("/dev/pts");

  ASSERT_ERROR_CODE(INTERNAL, fs_config_->SetupInsideNamespace(spec));
}

TEST_F(SetupInsideNamespace, EmptyFsSpec) {
  NamespaceSpec spec;
  spec.mutable_fs();

  // Same as above. Expect default setup.
  mock_file_lines_->ExpectFileLines(kProcMountsPath,
                                    proc_mount_contents_);
  EXPECT_CALL(mock_libc_fs_api_.Mock(), ChDir(StrEq("/")))
      .WillRepeatedly(Return(0));
  ExpectUnmounts(kProcMounts_ProcSys);
  ExpectUnmounts(kProcMounts_Stdfs);
  ExpectUnmounts(kProcMounts_CustomRootfs);

  EXPECT_CALL(mock_libc_fs_api_.Mock(), ChDir(StrEq("/")))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(mock_libc_fs_api_.Mock(),
              Mount(StrEq("proc"), StrEq("/proc/"), StrEq("proc"),
                    (MS_NODEV | MS_NOEXEC | MS_NOSUID | MS_RELATIME), nullptr))
      .WillOnce(Return(0));
  EXPECT_CALL(mock_libc_fs_api_.Mock(),
              Mount(StrEq("sysfs"), StrEq("/sys/"), StrEq("sysfs"),
                    (MS_NODEV | MS_NOEXEC | MS_NOSUID | MS_RELATIME), nullptr))
      .WillOnce(Return(0));
  ExpectDevptsSetupCalls();

  ASSERT_OK(fs_config_->SetupInsideNamespace(spec));
}

TEST_F(SetupInsideNamespace, CustomRootfs) {
  NamespaceSpec spec;
  FilesystemSpec *fs = spec.mutable_fs();
  fs->set_rootfs_path(kCustomRootfsPath);

  // Same as above. Expect default setup.
  mock_file_lines_->ExpectFileLines(kProcMountsPath,
                                    proc_mount_contents_);
  EXPECT_CALL(mock_libc_fs_api_.Mock(), ChDir(StrEq(kCustomRootfsPath)))
      .WillRepeatedly(Return(0));
  ExpectUnmounts(kProcMounts_ProcSys);
  ExpectUnmounts(kProcMounts_Stdfs);

  { // PivotRoot expectations
    EXPECT_CALL(mock_time_utils_.Mock(), MicrosecondsSinceEpoch())
        .WillOnce(Return(kTime));

    const string kOldRoot = Substitute("nscon.old_root.$0", kTime.value());
    EXPECT_CALL(mock_libc_fs_api_.Mock(), MkDir(StrEq(kOldRoot), 0700))
        .WillOnce(Return(0));
    EXPECT_CALL(mock_libc_fs_api_.Mock(),
                PivotRoot(StrEq("."), StrEq(kOldRoot))).WillOnce(Return(0));
    EXPECT_CALL(mock_libc_fs_api_.Mock(), ChDir(StrEq("/")))
        .WillOnce(Return(0));
    EXPECT_CALL(mock_libc_fs_api_.Mock(), UMount2(StrEq(kOldRoot), MNT_DETACH))
        .WillOnce(Return(0));
    EXPECT_CALL(mock_libc_fs_api_.Mock(), RmDir(StrEq(kOldRoot)))
        .WillOnce(Return(0));
  }

  EXPECT_CALL(mock_libc_fs_api_.Mock(),
              Mount(StrEq("proc"), StrEq("/proc/"), StrEq("proc"),
                    (MS_NODEV | MS_NOEXEC | MS_NOSUID | MS_RELATIME), nullptr))
      .WillOnce(Return(0));
  EXPECT_CALL(mock_libc_fs_api_.Mock(),
              Mount(StrEq("sysfs"), StrEq("/sys/"), StrEq("sysfs"),
                    (MS_NODEV | MS_NOEXEC | MS_NOSUID | MS_RELATIME), nullptr))
      .WillOnce(Return(0));
  ExpectDevptsSetupCalls();

  ASSERT_OK(fs_config_->SetupInsideNamespace(spec));
}

TEST_F(SetupInsideNamespace, CustomRootfs_AndExternalMounts) {
  NamespaceSpec spec;
  FilesystemSpec *fs = spec.mutable_fs();
  fs->set_rootfs_path(kCustomRootfsPath);
  AddMount(fs->mutable_external_mounts(), "/a", "/b", true, false);
  ExpectBindMount("/a", ::file::JoinPath(kCustomRootfsPath, "/b"), true, false,
                  Status::OK);

  // Same as above. Expect default setup.
  mock_file_lines_->ExpectFileLines(kProcMountsPath,
                                    proc_mount_contents_);
  EXPECT_CALL(mock_libc_fs_api_.Mock(), ChDir(StrEq(kCustomRootfsPath)))
      .WillRepeatedly(Return(0));
  ExpectUnmounts(kProcMounts_ProcSys);
  ExpectUnmounts(kProcMounts_Stdfs);

  { // PivotRoot expectations
    EXPECT_CALL(mock_time_utils_.Mock(), MicrosecondsSinceEpoch())
        .WillOnce(Return(kTime));

    const string kOldRoot = Substitute("nscon.old_root.$0", kTime.value());
    EXPECT_CALL(mock_libc_fs_api_.Mock(), MkDir(StrEq(kOldRoot), 0700))
        .WillOnce(Return(0));
    EXPECT_CALL(mock_libc_fs_api_.Mock(),
                PivotRoot(StrEq("."), StrEq(kOldRoot))).WillOnce(Return(0));
    EXPECT_CALL(mock_libc_fs_api_.Mock(), ChDir(StrEq("/")))
        .WillOnce(Return(0));
    EXPECT_CALL(mock_libc_fs_api_.Mock(), UMount2(StrEq(kOldRoot), MNT_DETACH))
        .WillOnce(Return(0));
    EXPECT_CALL(mock_libc_fs_api_.Mock(), RmDir(StrEq(kOldRoot)))
        .WillOnce(Return(0));
  }

  EXPECT_CALL(mock_libc_fs_api_.Mock(),
              Mount(StrEq("proc"), StrEq("/proc/"), StrEq("proc"),
                    (MS_NODEV | MS_NOEXEC | MS_NOSUID | MS_RELATIME), nullptr))
      .WillOnce(Return(0));
  EXPECT_CALL(mock_libc_fs_api_.Mock(),
              Mount(StrEq("sysfs"), StrEq("/sys/"), StrEq("sysfs"),
                    (MS_NODEV | MS_NOEXEC | MS_NOSUID | MS_RELATIME), nullptr))
      .WillOnce(Return(0));
  ExpectDevptsSetupCalls();

  ASSERT_OK(fs_config_->SetupInsideNamespace(spec));
}

TEST_F(SetupInsideNamespace, UseChroot) {
  NamespaceSpec spec;
  FilesystemSpec *fs = spec.mutable_fs();
  fs->set_rootfs_path(kCustomRootfsPath);
  fs->set_chroot_to_rootfs(true);

  // Same as above. Expect default setup.
  mock_file_lines_->ExpectFileLines(kProcMountsPath,
                                    proc_mount_contents_);
  EXPECT_CALL(mock_libc_fs_api_.Mock(), ChDir(StrEq(kCustomRootfsPath)))
      .WillRepeatedly(Return(0));
  ExpectUnmounts(kProcMounts_ProcSys);
  ExpectUnmounts(kProcMounts_Stdfs);

  { // ChRoot expectations
    EXPECT_CALL(mock_libc_fs_api_.Mock(), ChRoot(StrEq(kCustomRootfsPath)))
        .WillOnce(Return(0));
  }

  EXPECT_CALL(mock_libc_fs_api_.Mock(),
              Mount(StrEq("proc"), StrEq("/proc/"), StrEq("proc"),
                    (MS_NODEV | MS_NOEXEC | MS_NOSUID | MS_RELATIME), nullptr))
      .WillOnce(Return(0));
  EXPECT_CALL(mock_libc_fs_api_.Mock(),
              Mount(StrEq("sysfs"), StrEq("/sys/"), StrEq("sysfs"),
                    (MS_NODEV | MS_NOEXEC | MS_NOSUID | MS_RELATIME), nullptr))
      .WillOnce(Return(0));
  ExpectDevptsSetupCalls();

  ASSERT_OK(fs_config_->SetupInsideNamespace(spec));
}

// TODO(adityakali): Add failure tests for SetupInsideNamespace.

}  // namespace nscon
}  // namespace containers
