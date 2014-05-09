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

#include "nscon/ns_util.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/types.h>
#include <termios.h>

#include <memory>
#include <string>
using ::std::string;
#include <vector>

#include "util/errors_test_util.h"
#include "system_api/libc_fs_api_test_util.h"
#include "system_api/libc_process_api_test_util.h"
#include "file/base/path.h"
#include "gtest/gtest.h"

using ::std::vector;
using ::std::unique_ptr;
using ::util::error::INTERNAL;
using ::util::error::INVALID_ARGUMENT;
using ::util::Status;
using ::util::StatusOr;
using ::testing::AnyOf;
using ::testing::Assign;
using ::testing::Contains;
using ::testing::DoAll;
using ::testing::Eq;
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::NotNull;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::SetErrnoAndReturn;
using ::testing::StrEq;
using ::testing::_;

namespace containers {
namespace nscon {

// Used by CustomReadLink() to set linkdata for ReadLink syscall.
const char *kLinkData = "";

class NsUtilTest : public ::testing::Test {
 public:
  void SetUp() {
    internal::InitKnownNamespaces();
    // Assume only PID, IPC and UTS are supported for testing.
    ::std::set<int> namespaces = {CLONE_NEWPID, CLONE_NEWIPC, CLONE_NEWUTS};
    ns_util_.reset(new NsUtil(namespaces));
  }

  // Wrapper for private/protected methods.
  StatusOr<string> CallGetNamespaceId(pid_t pid, int ns) const {
    return ns_util_->GetNamespaceId(pid, ns);
  }

  SavedNamespace *NewSavedNamespace(int ns, int fd) {
    return new SavedNamespace(ns, fd);
  }

  void ExpectFileExists(const string &path, struct stat *statbuf) {
    statbuf->st_mode = 0;
    statbuf->st_mode |= S_IFCHR;
    EXPECT_CALL(libc_fs_api_.Mock(), Stat(StrEq(path), _))
        .WillOnce(DoAll(SetArgPointee<1>(*statbuf), Return(0)));
  }

  void ExpectFileNotExists(const string &path, int error) {
    EXPECT_CALL(libc_fs_api_.Mock(), Stat(StrEq(path), _))
        .WillOnce(SetErrnoAndReturn(error, -1));
  }

 protected:
  unique_ptr<NsUtil> ns_util_;
  system_api::MockLibcProcessApiOverride libc_process_api_;
  system_api::MockLibcFsApiOverride libc_fs_api_;
};

ssize_t CustomReadLink(const char *path, char *buf, size_t len) {
  ssize_t bytes = strlen(kLinkData);
  if (bytes > len) bytes = len;
  strncpy(buf, kLinkData, bytes);
  return bytes;
}

TEST_F(NsUtilTest, NsCloneFlagToName_InvalidFlags) {
  int flag = CLONE_FS;
  EXPECT_ERROR_CODE(INVALID_ARGUMENT, ns_util_->NsCloneFlagToName(flag));
}

TEST_F(NsUtilTest, NsCloneFlagToName_Success) {
  int flag = CLONE_NEWNS;
  ASSERT_OK(ns_util_->NsCloneFlagToName(flag));
}

TEST_F(NsUtilTest, AttachNamespaces_None) {
  vector<int> namespaces;

  ASSERT_OK(ns_util_->AttachNamespaces(namespaces, 9999));
}

TEST_F(NsUtilTest, AttachNamespaces_InvalidPid) {
  vector<int> namespaces = {CLONE_NEWIPC, CLONE_NEWNS};
  // Bad pid
  EXPECT_ERROR_CODE(INVALID_ARGUMENT,
                    ns_util_->AttachNamespaces(namespaces, 0));
}

TEST_F(NsUtilTest, AttachNamespaces_InvalidNs) {
  vector<int> namespaces = {CLONE_FS};  // Bad namespace flag
  EXPECT_ERROR_CODE(INVALID_ARGUMENT,
                    ns_util_->AttachNamespaces(namespaces, 9999));
}

TEST_F(NsUtilTest, AttachNamespaces_Selected) {
  vector<int> namespaces = {CLONE_NEWIPC, CLONE_NEWNS};
  int test_fd = 1000;
  int fd1 = test_fd++;
  int fd2 = test_fd++;
  EXPECT_CALL(libc_fs_api_.Mock(), Open(StrEq("/proc/9999/ns/ipc"), O_RDONLY))
      .WillOnce(Return(fd1));
  EXPECT_CALL(libc_fs_api_.Mock(), Open(StrEq("/proc/9999/ns/mnt"), O_RDONLY))
      .WillOnce(Return(fd2));
  EXPECT_CALL(libc_process_api_.Mock(), Setns(fd1, _)).WillOnce(Return(0));
  EXPECT_CALL(libc_process_api_.Mock(), Setns(fd2, _)).WillOnce(Return(0));
  EXPECT_CALL(libc_fs_api_.Mock(), Close(fd1)).WillOnce(Return(0));
  EXPECT_CALL(libc_fs_api_.Mock(), Close(fd2)).WillOnce(Return(0));
  ASSERT_OK(ns_util_->AttachNamespaces(namespaces, 9999));
}

TEST_F(NsUtilTest, AttachNamespaces_UsernsFirst) {
  vector<int> namespaces = {CLONE_NEWIPC, CLONE_NEWNS, CLONE_NEWUSER};
  int test_fd = 1000;
  int fd1 = test_fd++;
  int fd2 = test_fd++;
  int fd3 = test_fd++;
  EXPECT_CALL(libc_fs_api_.Mock(), Open(StrEq("/proc/9999/ns/ipc"), O_RDONLY))
      .WillOnce(Return(fd1));
  EXPECT_CALL(libc_fs_api_.Mock(), Open(StrEq("/proc/9999/ns/mnt"), O_RDONLY))
      .WillOnce(Return(fd2));
  EXPECT_CALL(libc_fs_api_.Mock(), Open(StrEq("/proc/9999/ns/user"), O_RDONLY))
      .WillOnce(Return(fd3));
  {
    InSequence userns_seq;

    // Make sure we Setns() to userns (fd3) first.
    EXPECT_CALL(libc_process_api_.Mock(), Setns(fd3, _)).WillOnce(Return(0));
    EXPECT_CALL(libc_process_api_.Mock(), Setns(fd1, _)).WillOnce(Return(0));
    EXPECT_CALL(libc_process_api_.Mock(), Setns(fd2, _)).WillOnce(Return(0));
  }

  EXPECT_CALL(libc_fs_api_.Mock(), Close(fd1)).WillOnce(Return(0));
  EXPECT_CALL(libc_fs_api_.Mock(), Close(fd2)).WillOnce(Return(0));
  EXPECT_CALL(libc_fs_api_.Mock(), Close(fd3)).WillOnce(Return(0));
  ASSERT_OK(ns_util_->AttachNamespaces(namespaces, 9999));
}

TEST_F(NsUtilTest, UnshareNamespaces_None) {
  vector<int> namespaces;
  ASSERT_OK(ns_util_->UnshareNamespaces(namespaces));
}

TEST_F(NsUtilTest, UnshareNamespaces_Invalid) {
  // CLONE_FS is a valid clone(2) flag, but not a valid unshare(2) flag.
  vector<int> namespaces = {CLONE_NEWIPC, CLONE_NEWNS, CLONE_FS};
  EXPECT_ERROR_CODE(INVALID_ARGUMENT, ns_util_->UnshareNamespaces(namespaces));
}

TEST_F(NsUtilTest, UnshareNamespaces_Selected) {
  vector<int> namespaces = {CLONE_NEWIPC, CLONE_NEWNS};
  EXPECT_CALL(libc_process_api_.Mock(), Unshare(CLONE_NEWIPC|CLONE_NEWNS))
      .WillOnce(Return(0));
  ASSERT_OK(ns_util_->UnshareNamespaces(namespaces));
}

TEST_F(NsUtilTest, IsNamespaceSupported) {
  // Only pid, ipc and uts are supported for testing.
  EXPECT_TRUE(ns_util_->IsNamespaceSupported(CLONE_NEWPID));
  EXPECT_TRUE(ns_util_->IsNamespaceSupported(CLONE_NEWIPC));
  EXPECT_TRUE(ns_util_->IsNamespaceSupported(CLONE_NEWUTS));
  EXPECT_FALSE(ns_util_->IsNamespaceSupported(CLONE_NEWUSER));
  EXPECT_FALSE(ns_util_->IsNamespaceSupported(CLONE_NEWNET));
  EXPECT_FALSE(ns_util_->IsNamespaceSupported(CLONE_NEWNS));
}

TEST_F(NsUtilTest, GetNamespaceId_Self) {
  const char *kTestNsId = "pid:[1111]";
  EXPECT_CALL(libc_fs_api_.Mock(), ReadLink(StrEq("/proc/self/ns/pid"),
                                            NotNull(), _))
      .WillOnce(DoAll(Assign(&kLinkData, kTestNsId), Invoke(&CustomReadLink)));
  StatusOr<string> statusor = CallGetNamespaceId(0, CLONE_NEWPID);
  ASSERT_OK(statusor);
  EXPECT_STREQ(kTestNsId, statusor.ValueOrDie().c_str());
}

TEST_F(NsUtilTest, GetNamespaceId_Pid) {
  const pid_t kPid = 9999;
  const char *kTestNsId = "pid:[1111]";
  EXPECT_CALL(libc_fs_api_.Mock(), ReadLink(StrEq("/proc/9999/ns/pid"),
                                            NotNull(), _))
      .WillOnce(DoAll(Assign(&kLinkData, kTestNsId), Invoke(&CustomReadLink)));
  StatusOr<string> statusor = CallGetNamespaceId(kPid, CLONE_NEWPID);
  ASSERT_OK(statusor);
  EXPECT_STREQ(kTestNsId, statusor.ValueOrDie().c_str());
}

TEST_F(NsUtilTest, GetNamespaceId_BadPid) {
  const pid_t kPid = -1111;
  EXPECT_ERROR_CODE(INVALID_ARGUMENT, CallGetNamespaceId(kPid, CLONE_NEWPID));
}

TEST_F(NsUtilTest, GetNamespaceId_BadNamespace) {
  const pid_t kPid = 9999;
  EXPECT_ERROR_CODE(INVALID_ARGUMENT,
                    CallGetNamespaceId(kPid, CLONE_VFORK));
}

TEST_F(NsUtilTest, GetUnsharedNamespaces) {
  const pid_t kPid = 9999;
  // Out of the supported PID, IPC and UTS, assume that PID and IPC are
  // unshared.
  EXPECT_CALL(libc_fs_api_.Mock(), ReadLink(StrEq("/proc/self/ns/pid"),
                                            NotNull(), _))
      .WillOnce(DoAll(Assign(&kLinkData, "1111"), Invoke(&CustomReadLink)));
  EXPECT_CALL(libc_fs_api_.Mock(), ReadLink(StrEq("/proc/self/ns/ipc"),
                                            NotNull(), _))
      .WillOnce(DoAll(Assign(&kLinkData, "2222"), Invoke(&CustomReadLink)));
  EXPECT_CALL(libc_fs_api_.Mock(), ReadLink(StrEq("/proc/self/ns/uts"),
                                            NotNull(), _))
      .WillOnce(DoAll(Assign(&kLinkData, "3333"), Invoke(&CustomReadLink)));
  EXPECT_CALL(libc_fs_api_.Mock(), ReadLink(StrEq("/proc/9999/ns/pid"),
                                            NotNull(), _))
      .WillOnce(DoAll(Assign(&kLinkData, "5555"), Invoke(&CustomReadLink)));
  EXPECT_CALL(libc_fs_api_.Mock(), ReadLink(StrEq("/proc/9999/ns/ipc"),
                                            NotNull(), _))
      .WillOnce(DoAll(Assign(&kLinkData, "6666"), Invoke(&CustomReadLink)));
  EXPECT_CALL(libc_fs_api_.Mock(), ReadLink(StrEq("/proc/9999/ns/uts"),
                                            NotNull(), _))
      .WillOnce(DoAll(Assign(&kLinkData, "3333"), Invoke(&CustomReadLink)));

  StatusOr<const vector<int>> statusor = ns_util_->GetUnsharedNamespaces(kPid);
  ASSERT_OK(statusor);
  vector<int> namespaces = statusor.ValueOrDie();
  EXPECT_EQ(2, namespaces.size());
  EXPECT_THAT(namespaces, Contains(CLONE_NEWPID));
  EXPECT_THAT(namespaces, Contains(CLONE_NEWIPC));
}

TEST_F(NsUtilTest, SaveNamespace) {
  const int kNs = CLONE_NEWPID;
  const int kNsFd = 33;
  EXPECT_CALL(libc_fs_api_.Mock(), Open(StrEq("/proc/self/ns/pid"), O_RDONLY))
      .WillOnce(Return(kNsFd));
  EXPECT_CALL(libc_fs_api_.Mock(), Close(kNsFd)).WillOnce(Return(0));

  StatusOr<SavedNamespace *> statusor = ns_util_->SaveNamespace(kNs);
  ASSERT_OK(statusor);
  delete statusor.ValueOrDie();
}

TEST_F(NsUtilTest, SaveNamespace_OpenFailure) {
  const int kNs = CLONE_NEWPID;
  EXPECT_CALL(libc_fs_api_.Mock(), Open(StrEq("/proc/self/ns/pid"), O_RDONLY))
      .WillOnce(SetErrnoAndReturn(EINVAL, -1));

  EXPECT_ERROR_CODE(INTERNAL, ns_util_->SaveNamespace(kNs));
}

TEST_F(NsUtilTest, RestoreAndDelete) {
  const int kNs = CLONE_NEWPID;
  const int kNsFd = 1805;
  unique_ptr<SavedNamespace> saved_ns(NewSavedNamespace(kNs, kNsFd));
  EXPECT_CALL(libc_process_api_.Mock(), Setns(kNsFd, 0)).WillOnce(Return(0));
  EXPECT_CALL(libc_fs_api_.Mock(), Close(kNsFd)).WillOnce(Return(0));

  ASSERT_OK(saved_ns.release()->RestoreAndDelete());
}

TEST_F(NsUtilTest, RestoreAndDelete_SetnsFailure) {
  const int kNs = CLONE_NEWPID;
  const int kNsFd = 1805;
  unique_ptr<SavedNamespace> saved_ns(NewSavedNamespace(kNs, kNsFd));
  EXPECT_CALL(libc_process_api_.Mock(), Setns(kNsFd, 0))
      .WillOnce(SetErrnoAndReturn(EBADF, -1));
  EXPECT_CALL(libc_fs_api_.Mock(), Close(kNsFd)).WillOnce(Return(0));
  EXPECT_ERROR_CODE(INTERNAL, saved_ns->RestoreAndDelete());
}

TEST_F(NsUtilTest, RestoreAndDelete_CloseFailure) {
  const int kNs = CLONE_NEWPID;
  const int kNsFd = 1805;
  unique_ptr<SavedNamespace> saved_ns(NewSavedNamespace(kNs, kNsFd));
  EXPECT_CALL(libc_process_api_.Mock(), Setns(kNsFd, 0)).WillOnce(Return(0));
  EXPECT_CALL(libc_fs_api_.Mock(), Close(kNsFd))
      .WillRepeatedly(SetErrnoAndReturn(EBADF, -1));

  EXPECT_ERROR_CODE(INTERNAL, saved_ns->RestoreAndDelete());
}

typedef NsUtilTest CharacterDeviceFileExistsTest;
static const char *kCharDevFile = "/dev/pts/0";

TEST_F(CharacterDeviceFileExistsTest, Success) {
  struct stat statbuf;
  ExpectFileExists(kCharDevFile, &statbuf);

  EXPECT_OK(ns_util_->CharacterDeviceFileExists(kCharDevFile));
}

TEST_F(CharacterDeviceFileExistsTest, StatFailure) {
  ExpectFileNotExists(kCharDevFile, EACCES);

  EXPECT_ERROR_CODE(INTERNAL,
                    ns_util_->CharacterDeviceFileExists(kCharDevFile));
}

TEST_F(CharacterDeviceFileExistsTest, NotExistsFailure) {
  ExpectFileNotExists(kCharDevFile, ENOENT);

  EXPECT_ERROR_CODE(INVALID_ARGUMENT,
                    ns_util_->CharacterDeviceFileExists(kCharDevFile));
}

TEST_F(CharacterDeviceFileExistsTest, NotCharDevFailure) {
  struct stat statbuf;
  statbuf.st_mode = 0;
  statbuf.st_mode |= S_IFDIR;
  EXPECT_CALL(libc_fs_api_.Mock(), Stat(StrEq(kCharDevFile), _))
      .WillOnce(DoAll(SetArgPointee<1>(statbuf), Return(0)));

  EXPECT_ERROR_CODE(INVALID_ARGUMENT,
                    ns_util_->CharacterDeviceFileExists(kCharDevFile));
}

typedef NsUtilTest OpenSlavePtyDeviceTest;

static const char *kDevPts = "10";
static const string &kSlavePtyPath = file::JoinPath("/dev/pts", kDevPts);
static const int kSlaveFd = 5;

TEST_F(OpenSlavePtyDeviceTest, Success) {
  struct stat statbuf;
  ExpectFileExists(kSlavePtyPath, &statbuf);
  EXPECT_CALL(libc_fs_api_.Mock(), Open(StrEq(kSlavePtyPath), Eq(O_RDWR)))
      .WillOnce(Return(kSlaveFd));
  EXPECT_EQ(kSlaveFd, ns_util_->OpenSlavePtyDevice(kDevPts).ValueOrDie());
}

TEST_F(OpenSlavePtyDeviceTest, FailureFileNotExist) {
  ExpectFileNotExists(kSlavePtyPath, ENOENT);

  EXPECT_ERROR_CODE(INVALID_ARGUMENT, ns_util_->OpenSlavePtyDevice(kDevPts));
}

TEST_F(OpenSlavePtyDeviceTest, FailureOpenError) {
  struct stat statbuf;
  ExpectFileExists(kSlavePtyPath, &statbuf);

  EXPECT_CALL(libc_fs_api_.Mock(), Open(StrEq(kSlavePtyPath), Eq(O_RDWR)))
      .WillOnce(SetErrnoAndReturn(ENOENT, -1));
  EXPECT_ERROR_CODE(INTERNAL, ns_util_->OpenSlavePtyDevice(kDevPts));
}

typedef NsUtilTest AttachToConsoleFdTest;

TEST_F(AttachToConsoleFdTest, Success) {
  EXPECT_CALL(libc_fs_api_.Mock(), Close(Eq(kSlaveFd)));

  EXPECT_CALL(libc_fs_api_.Mock(), Dup2(kSlaveFd, AnyOf(Eq(0),
                                                        Eq(1),
                                                        Eq(2))))
      .WillRepeatedly(Return(0));
#ifdef TIOCTTY
  EXPECT_CALL(libc_fs_api_.Mock(), Ioctl(Eq(kSlaveFd), TIOCTTY, 0))
      .WillOnce(Return(0));
#endif
  ASSERT_OK(ns_util_->AttachToConsoleFd(kSlaveFd));
}

TEST_F(AttachToConsoleFdTest, Failure_DupError) {
  EXPECT_CALL(libc_fs_api_.Mock(), Dup2(kSlaveFd, AnyOf(Eq(0),
                                                        Eq(1),
                                                        Eq(2))))
      .WillOnce(Return(0))
      .WillOnce(SetErrnoAndReturn(EPERM, -1));
  ASSERT_ERROR_CODE(::util::error::INTERNAL,
                    ns_util_->AttachToConsoleFd(kSlaveFd));
}

#ifdef TIOSCTTY
TEST_F(AttachToConsoleFdTest, Failure_IoctlError) {
  EXPECT_CALL(libc_fs_api_.Mock(), Close(Eq(kSlaveFd)));

  EXPECT_CALL(libc_fs_api_.Mock(), Dup2(kSlaveFd, AnyOf(Eq(0),
                                                        Eq(1),
                                                        Eq(2))))
      .WillRepeatedly(Return(0));

  EXPECT_CALL(libc_fs_api_.Mock(), Ioctl(Eq(kSlaveFd), TIOCTTY, 0))
      .WillOnce(Return(0));

  ASSERT_ERROR_CODE(::util::error::INTERNAL,
                    ns_util_->AttachToConsoleFd(kSlaveFd));
}
#endif

typedef NsUtilTest GetOpenFDsTest;

TEST_F(GetOpenFDsTest, Success) {
  DIR *test_dir = reinterpret_cast<DIR *>(0xFFFFFFFF11223344);
  EXPECT_CALL(libc_fs_api_.Mock(), OpenDir(StrEq("/proc/self/fd/")))
      .WillOnce(Return(test_dir));
  EXPECT_CALL(libc_fs_api_.Mock(), ReadDirR(test_dir, NotNull(), NotNull()))
      .WillOnce(DoAll(SetArgPointee<2>(nullptr), Return(0)));
  EXPECT_CALL(libc_fs_api_.Mock(), CloseDir(test_dir))
      .WillOnce(Return(0));
  StatusOr<vector<int>> statusor = ns_util_->GetOpenFDs();
  ASSERT_OK(statusor);
  vector<int> fd_list = statusor.ValueOrDie();
  EXPECT_EQ(0, fd_list.size());
}

TEST_F(GetOpenFDsTest, SuccessWithFdList) {
  DIR *test_dir = reinterpret_cast<DIR *>(0xFFFFFFFF11223344);
  struct dirent fd1 = {0, 0, 0, 0, "10" /* d_name */},
                fd2 = {0, 0, 0, 0, "20" /* d_name */},
                fd3 = {0, 0, 0, 0, "30" /* d_name */};

  EXPECT_CALL(libc_fs_api_.Mock(), OpenDir(StrEq("/proc/self/fd/")))
      .WillOnce(Return(test_dir));
  {
    InSequence readdir_seq;
    EXPECT_CALL(libc_fs_api_.Mock(), ReadDirR(test_dir, NotNull(), NotNull()))
        .WillOnce(DoAll(SetArgPointee<1>(fd1), SetArgPointee<2>(&fd1),
                        Return(0)));
    EXPECT_CALL(libc_fs_api_.Mock(), ReadDirR(test_dir, NotNull(), NotNull()))
        .WillOnce(DoAll(SetArgPointee<1>(fd2), SetArgPointee<2>(&fd2),
                        Return(0)));
    EXPECT_CALL(libc_fs_api_.Mock(), ReadDirR(test_dir, NotNull(), NotNull()))
        .WillOnce(DoAll(SetArgPointee<1>(fd3), SetArgPointee<2>(&fd3),
                        Return(0)));
    EXPECT_CALL(libc_fs_api_.Mock(), ReadDirR(test_dir, NotNull(), NotNull()))
        .WillOnce(DoAll(SetArgPointee<2>(nullptr), Return(0)));
  }
  EXPECT_CALL(libc_fs_api_.Mock(), CloseDir(test_dir))
      .WillOnce(Return(0));
  StatusOr<vector<int>> statusor = ns_util_->GetOpenFDs();
  ASSERT_OK(statusor);
  vector<int> fd_list = statusor.ValueOrDie();
  EXPECT_THAT(fd_list, Contains(10));
  EXPECT_THAT(fd_list, Contains(20));
  EXPECT_THAT(fd_list, Contains(30));
  EXPECT_EQ(3, fd_list.size());
}

TEST_F(GetOpenFDsTest, ReadDirRFailure) {
  DIR *test_dir = reinterpret_cast<DIR *>(0xFFFFFFFF11223344);
  EXPECT_CALL(libc_fs_api_.Mock(), OpenDir(StrEq("/proc/self/fd/")))
      .WillOnce(Return(test_dir));
  EXPECT_CALL(libc_fs_api_.Mock(), ReadDirR(test_dir, NotNull(), NotNull()))
      .WillOnce(Return(EBADF));
  EXPECT_CALL(libc_fs_api_.Mock(), CloseDir(test_dir))
      .WillOnce(Return(0));
  ASSERT_ERROR_CODE(::util::error::INTERNAL, ns_util_->GetOpenFDs());
}

TEST_F(GetOpenFDsTest, OpenDirFailure) {
  EXPECT_CALL(libc_fs_api_.Mock(), OpenDir(StrEq("/proc/self/fd/")))
      .WillOnce(SetErrnoAndReturn(EACCES, nullptr));
  ASSERT_ERROR_CODE(::util::error::INTERNAL, ns_util_->GetOpenFDs());
}

}  // namespace nscon
}  // namespace containers
