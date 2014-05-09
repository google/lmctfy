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

#include "lmctfy/util/console_util.h"

#include <sys/mount.h>

#include <string>
using ::std::string;
#include <vector>

#include "global_utils/fs_utils_test_util.h"
#include "global_utils/mount_utils_test_util.h"
#include "system_api/libc_fs_api_test_util.h"
#include "util/errors_test_util.h"
#include "util/file_lines_test_util.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::util::FileLinesTestUtil;
using ::util::MockFsUtilsOverride;
using ::system_api::MockLibcFsApiOverride;
using ::util::MockMountUtilsOverride;
using ::testing::_;
using ::testing::Return;
using ::testing::SetErrnoAndReturn;
using ::testing::StrEq;
using ::util::error::INTERNAL;
using ::util::Status;

namespace containers {

static const vector<string> kProcMountInfoLines = {
  "70 17 0:11 /ptmx /dev/ptmx rw,nosuid,noexec,relatime - "
  "devpts devpts rw,mode=600,ptmxmode=666"
};

class ConsoleUtilTest : public ::testing::Test {
 public:
  ConsoleUtilTest() : kNoPerm({}),
                      kDesiredPerm({}),
                      file_lines_(&mock_libc_fs_api_) {
    kNoPerm.st_mode = 0000;
    kDesiredPerm.st_mode = 0666;
  }

 protected:
  void ExpectPathExists(const string &path) {
    EXPECT_CALL(mock_fs_utils_.Mock(), FileExists(StrEq(path)))
        .WillOnce(Return(true));
  }

  void ExpectPathNotExists(const string &path) {
    EXPECT_CALL(mock_fs_utils_.Mock(), FileExists(StrEq(path)))
        .WillOnce(Return(false));
  }

  void ExpectStat(const string &path, struct stat statbuf) {
    EXPECT_CALL(mock_libc_fs_api_.Mock(), Stat(StrEq(path), _)).WillOnce(
        ::testing::DoAll(::testing::SetArgPointee<1>(statbuf), Return(0)));
  }

  void ExpectMountInfo(const vector<string> mount_info_lines) {
    file_lines_.ExpectFileLines("/proc/1/mountinfo", mount_info_lines);
  }

  void ExpectDevptsMount(int error) {
    EXPECT_CALL(mock_libc_fs_api_.Mock(),
                Mount(StrEq("devpts"), StrEq("/dev/pts"), StrEq("devpts"),
                      (MS_NOEXEC | MS_NOSUID | MS_RELATIME), _))
        .WillOnce(::testing::InvokeWithoutArgs([error]() {
              if (error == 0) {
                return 0;
              } else {
                errno = error;
                return -1;
              }
            }));
  }

  void ExpectBindMount(const string &source, const string &target,
                       Status result) {
    ::std::set<::util::MountUtils::BindMountOpts> opts;
    EXPECT_CALL(mock_mount_utils_.Mock(),
                BindMount(StrEq(source),
                          StrEq(target),
                          ::testing::ContainerEq(opts)))
        .WillOnce(Return(result));
  }

  Status CallInitDevPtsNamespace() {
    return console_util_.EnableDevPtsNamespaceSupport();
  }

  struct stat kNoPerm;
  struct stat kDesiredPerm;

  ConsoleUtil console_util_;
  FileLinesTestUtil file_lines_;
  MockFsUtilsOverride mock_fs_utils_;
  MockMountUtilsOverride mock_mount_utils_;
  MockLibcFsApiOverride mock_libc_fs_api_;
};

TEST_F(ConsoleUtilTest, SuccessNoDevptsNamespace) {
  ExpectPathExists("/dev/pts");
  ExpectPathExists("/dev/ptmx");
  ExpectPathExists("/dev/pts/ptmx");

  ExpectStat("/dev/pts/ptmx", kNoPerm);

  ExpectDevptsMount(0);
  ExpectMountInfo({});
  ExpectBindMount("/dev/pts/ptmx", "/dev/ptmx", Status::OK);

  EXPECT_OK(CallInitDevPtsNamespace());
}

TEST_F(ConsoleUtilTest, Success_DevptsNotExists) {
  ExpectPathNotExists("/dev/pts");

  EXPECT_OK(CallInitDevPtsNamespace());
}

TEST_F(ConsoleUtilTest, Success_DevptmxNotExists) {
  ExpectPathExists("/dev/pts");
  ExpectPathNotExists("/dev/ptmx");

  EXPECT_OK(CallInitDevPtsNamespace());
}

TEST_F(ConsoleUtilTest, Success_DevptsptmxNotExists) {
  ExpectPathExists("/dev/pts");
  ExpectPathExists("/dev/ptmx");
  ExpectPathNotExists("/dev/pts/ptmx");

  EXPECT_OK(CallInitDevPtsNamespace());
}

TEST_F(ConsoleUtilTest, Failure_StatError) {
  ExpectPathExists("/dev/pts");
  ExpectPathExists("/dev/ptmx");
  ExpectPathExists("/dev/pts/ptmx");
  EXPECT_CALL(mock_libc_fs_api_.Mock(), Stat(StrEq("/dev/pts/ptmx"), _))
      .WillOnce(SetErrnoAndReturn(ENOENT, -1));

  EXPECT_ERROR_CODE(INTERNAL, CallInitDevPtsNamespace());
}

TEST_F(ConsoleUtilTest, Failure_DevptsMountError) {
  ExpectPathExists("/dev/pts");
  ExpectPathExists("/dev/ptmx");
  ExpectPathExists("/dev/pts/ptmx");
  ExpectStat("/dev/pts/ptmx", kNoPerm);
  ExpectDevptsMount(EPERM);

  EXPECT_ERROR_CODE(INTERNAL, CallInitDevPtsNamespace());
}

TEST_F(ConsoleUtilTest, Failure_BindMountError) {
  ExpectPathExists("/dev/pts");
  ExpectPathExists("/dev/ptmx");
  ExpectPathExists("/dev/pts/ptmx");
  ExpectStat("/dev/pts/ptmx", kNoPerm);
  ExpectDevptsMount(0);
  ExpectMountInfo({});
  ExpectBindMount("/dev/pts/ptmx", "/dev/ptmx", Status(INTERNAL, "blah"));

  EXPECT_ERROR_CODE(INTERNAL, CallInitDevPtsNamespace());
}

TEST_F(ConsoleUtilTest, Failure_DevptsPtmxFileExistsError) {
  ExpectPathExists("/dev/pts");
  ExpectPathExists("/dev/ptmx");
  EXPECT_CALL(mock_fs_utils_.Mock(), FileExists(StrEq("/dev/pts/ptmx")))
      .WillOnce(Return(Status(INTERNAL, "blah")));

  EXPECT_ERROR_CODE(INTERNAL, CallInitDevPtsNamespace());
}

TEST_F(ConsoleUtilTest, Success_NothingToDo) {
  ExpectPathExists("/dev/pts");
  ExpectPathExists("/dev/ptmx");
  ExpectPathExists("/dev/pts/ptmx");
  ExpectStat("/dev/pts/ptmx", kDesiredPerm);
  ExpectMountInfo(kProcMountInfoLines);

  EXPECT_OK(CallInitDevPtsNamespace());
}

}  // namespace containers
