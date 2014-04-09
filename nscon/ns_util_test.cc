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

#include <fcntl.h>  // for O_RDONLY
#include <memory>
#include <string>
using ::std::string;
#include <vector>

#include "util/errors_test_util.h"
#include "system_api/libc_fs_api_test_util.h"
#include "system_api/libc_process_api_test_util.h"
#include "gtest/gtest.h"

using ::std::vector;
using ::std::unique_ptr;
using ::util::error::INTERNAL;
using ::util::error::INVALID_ARGUMENT;
using ::util::Status;
using ::util::StatusOr;
using ::testing::Assign;
using ::testing::Contains;
using ::testing::DoAll;
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
  statbuf.st_mode = 0;
  statbuf.st_mode |= S_IFCHR;
  EXPECT_CALL(libc_fs_api_.Mock(), Stat(StrEq(kCharDevFile), _))
      .WillOnce(DoAll(SetArgPointee<1>(statbuf), Return(0)));

  EXPECT_OK(ns_util_->CharacterDeviceFileExists(kCharDevFile));
}

TEST_F(CharacterDeviceFileExistsTest, StatFailure) {
  EXPECT_CALL(libc_fs_api_.Mock(), Stat(StrEq(kCharDevFile), _))
      .WillOnce(SetErrnoAndReturn(EACCES, -1));

  EXPECT_ERROR_CODE(INTERNAL,
                    ns_util_->CharacterDeviceFileExists(kCharDevFile));
}

TEST_F(CharacterDeviceFileExistsTest, NotExistsFailure) {
  EXPECT_CALL(libc_fs_api_.Mock(), Stat(StrEq(kCharDevFile), _))
      .WillOnce(SetErrnoAndReturn(ENOENT, -1));

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

}  // namespace nscon
}  // namespace containers
