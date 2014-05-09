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

#ifndef SYSTEM_LIBC_FS_API_TEST_SYSTEM_H_
#define SYSTEM_LIBC_FS_API_TEST_SYSTEM_H_

#include <dirent.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#include "base/macros.h"
#include "system_api/libc_fs_api.h"
#include "gmock/gmock.h"

namespace system_api {

class MockLibcFsApi : public LibcFsApi {
 public:
  MOCK_CONST_METHOD2(FOpen, FILE *(const char *path, const char *mode));
  MOCK_CONST_METHOD2(FdOpen, FILE *(int file_descriptor, const char *mode));
  MOCK_CONST_METHOD3(FReopen,
                     FILE *(const char *path, const char *mode, FILE *stream));
  MOCK_CONST_METHOD1(OpenDir, DIR *(const char *name));
  MOCK_CONST_METHOD2(Open, int(const char *path, int oflag));
  MOCK_CONST_METHOD3(OpenWithMode, int(const char *path, int oflag, int mode));
  MOCK_CONST_METHOD1(FClose, int(FILE *file_pointer));
  MOCK_CONST_METHOD3(FScanfUU, int(FILE *file_pointer, unsigned int *first,
                                   unsigned int *second));
  MOCK_CONST_METHOD1(Close, int(int file_descriptor));
  MOCK_CONST_METHOD2(ChMod, int(const char *path, mode_t mode));
  MOCK_CONST_METHOD3(ChOwn, int(const char *path, uid_t owner, gid_t group));
  MOCK_CONST_METHOD3(LChOwn, int(const char *path, uid_t owner, gid_t group));
  MOCK_CONST_METHOD3(FChOwn, int(int fd, uid_t owner, gid_t group));
  MOCK_CONST_METHOD3(MkNod, int(const char *path, mode_t mode, dev_t dev));
  MOCK_CONST_METHOD1(Unlink, int(const char *path));
  MOCK_CONST_METHOD2(Rename, int(const char *oldpath, const char *newpath));
  MOCK_CONST_METHOD2(MkDir, int(const char *path, mode_t mode));
  MOCK_CONST_METHOD1(RmDir, int(const char *path));
  MOCK_CONST_METHOD2(Stat, int(const char *path, struct stat *buf));
  MOCK_CONST_METHOD2(Stat64, int(const char *path, struct stat64 *buf));
  MOCK_CONST_METHOD2(LStat, int(const char *path, struct stat *buf));
  MOCK_CONST_METHOD2(FStat, int(int file_descriptor, struct stat *buf));
  MOCK_CONST_METHOD2(StatFs64, int(const char *path, struct statfs64 *buf));
  MOCK_CONST_METHOD5(Mount, int(const char *source, const char *target,
                                const char *filesystemtype,
                                unsigned long mountflags,  // NOLINT
                                const void *data));
  MOCK_CONST_METHOD1(UMount, int(const char *target));
  MOCK_CONST_METHOD2(UMount2, int(const char *target, int flags));
  MOCK_CONST_METHOD4(FRead,
                     int(void *ptr, size_t size, size_t nmemb, FILE *stream));
  MOCK_CONST_METHOD4(FWrite, int(const void *ptr, size_t size, size_t nmemb,
                                 FILE *stream));
  MOCK_CONST_METHOD3(FGetS, char *(char *buf, int n, FILE *));
  MOCK_CONST_METHOD1(FError, int(FILE *stream));
  MOCK_CONST_METHOD3(Read,
                     ssize_t(int file_descriptor, char *buf, size_t nbytes));
  MOCK_CONST_METHOD3(Write, ssize_t(int file_descriptor, const void *buf,
                                    size_t nbytes));
  MOCK_CONST_METHOD1(FSync, int(int file_descriptor));
  MOCK_CONST_METHOD1(ChDir, int(const char *path));
  MOCK_CONST_METHOD3(ReadDirR, int(DIR *dir, dirent *entry, dirent **result));
  MOCK_CONST_METHOD1(CloseDir, int(DIR *dir));
  MOCK_CONST_METHOD3(ReadLink,
                     ssize_t(const char *path, char *buf, size_t len));
  MOCK_CONST_METHOD2(SymLink, int(const char *from, const char *to));
  MOCK_CONST_METHOD2(Link, int(const char *from, const char *to));
  MOCK_CONST_METHOD2(RealPath, char *(const char *name, char *resolved));
  MOCK_CONST_METHOD2(Access, int(const char *name, int type));
  MOCK_CONST_METHOD3(FnMatch,
                     int(const char *pattern, const char *string, int flags));
  MOCK_CONST_METHOD3(Ioctl, int(int fd, int request, void *argp));
  MOCK_CONST_METHOD1(Pipe, int(int pipefd[2]));
  MOCK_CONST_METHOD2(Pipe2, int(int pipefd[2], int flags));
  MOCK_CONST_METHOD1(ChRoot, int(const char *path));
  MOCK_CONST_METHOD2(PivotRoot, int(const char *new_root, const char *put_old));
  MOCK_CONST_METHOD2(Dup2, int(int oldfd, int newfd));
  MOCK_CONST_METHOD3(FCntl, int(int fd, int cmd, int arg1));
};

extern const LibcFsApi *GlobalLibcFsApi();

class MockLibcFsApiOverride {
 public:
  ::testing::StrictMock<MockLibcFsApi> &Mock() {
    ::testing::StrictMock<MockLibcFsApi>* ptr =
        (::testing::StrictMock<MockLibcFsApi>*)GlobalLibcFsApi();
    return *ptr;
  }
};

}  // namespace system_api

#endif  // SYSTEM_LIBC_FS_API_TEST_SYSTEM_H_
