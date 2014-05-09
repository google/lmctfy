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

#ifndef SYSTEM_LIBC_FS_API_IMPL_H_
#define SYSTEM_LIBC_FS_API_IMPL_H_

#include <dirent.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>

#include "base/macros.h"
#include "system_api/libc_fs_api.h"

namespace system_api {

// Used in "real" Singleton::CreateInstance() hooks for non-test binaries.
class LibcFsApiImpl : public LibcFsApi {
 public:
  LibcFsApiImpl() {}

  virtual ~LibcFsApiImpl() {}

  virtual FILE *FOpen(const char *path, const char *mode) const;

  virtual FILE *FdOpen(int file_descriptor, const char *mode) const;

  virtual FILE *FReopen(const char *path, const char *mode, FILE *stream) const;

  virtual DIR *OpenDir(const char *name) const;

  virtual int Open(const char *path, int oflag) const;

  virtual int OpenWithMode(const char *path, int oflag, int mode) const;

  virtual int FClose(FILE *file_pointer) const;

  virtual int FScanfUU(FILE *file_pointer, unsigned int *first,
                       unsigned int *second) const;

  virtual int Close(int file_descriptor) const;

  virtual int ChMod(const char *path, mode_t mode) const;

  virtual int ChOwn(const char *path, uid_t owner, gid_t group) const;

  virtual int LChOwn(const char *path, uid_t owner, gid_t group) const;

  virtual int FChOwn(int fd, uid_t owner, gid_t group) const;

  virtual int Rename(const char *oldpath, const char *newpath) const;

  virtual int MkNod(const char *path, mode_t mode, dev_t dev) const;

  virtual int Unlink(const char *path) const;

  virtual int MkDir(const char *path, mode_t mode) const;

  virtual int RmDir(const char *path) const;

  virtual int Stat(const char *path, struct stat *buf) const;

  virtual int Stat64(const char *path, struct stat64 *buf) const;

  virtual int LStat(const char *path, struct stat *buf) const;

  virtual int FStat(int file_descriptor, struct stat *buf) const;

  virtual int StatFs64(const char *path, struct statfs64 *buf) const;

  virtual int Mount(const char *source, const char *target,
                    const char *filesystemtype,
                    unsigned long mountflags,  // NOLINT
                    const void *data) const;

  virtual int UMount(const char *target) const;

  virtual int UMount2(const char *target, int flags) const;

  virtual int FRead(void *ptr, size_t size, size_t nmemb, FILE *stream) const;

  virtual int FWrite(const void *ptr, size_t size, size_t nmemb,
                     FILE *stream) const;

  virtual char *FGetS(char *buf, int n, FILE *stream) const;

  virtual int FError(FILE *stream) const;

  virtual ssize_t Read(int file_descriptor, char *buf, size_t nbytes) const;

  virtual ssize_t Write(int file_descriptor, const void *buf,
                        size_t nbytes) const;

  virtual int FSync(int file_descriptor) const;

  virtual int ChDir(const char *path) const;

  virtual int ReadDirR(DIR *dir, dirent *entry, dirent **result) const;

  virtual int CloseDir(DIR *dir) const;

  virtual ssize_t ReadLink(const char *path, char *buf, size_t len) const;

  virtual int SymLink(const char *from, const char *to) const;

  virtual int Link(const char *from, const char *to) const;

  virtual char *RealPath(const char *name, char *resolved) const;

  virtual int Access(const char *name, int type) const;

  virtual int FnMatch(const char *pattern, const char *string, int flags) const;

  virtual int Ioctl(int fd, int request, void *argp) const;

  virtual int Pipe(int pipefd[2]) const;

  virtual int Pipe2(int pipefd[2], int flags) const;

  virtual int ChRoot(const char *path) const;

  virtual int PivotRoot(const char *new_root, const char *put_old) const;

  virtual int Dup2(int olfd, int newfd) const;

  virtual int FCntl(int fd, int cmd, int arg1) const;

 private:
  DISALLOW_COPY_AND_ASSIGN(LibcFsApiImpl);
};

}  // namespace system_api

#endif  // SYSTEM_LIBC_FS_API_IMPL_H_
