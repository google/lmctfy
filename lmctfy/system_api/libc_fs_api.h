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

// The point of this is to enable injecting and mocking out filesystem
// interaction in unit tests. We define methods that (in production code)
// forward to the standard functions defined in stdio.h, but can be overridden
// using the tools provided in libcfs_test_util.h for testing.

#ifndef SYSTEM_LIBC_FS_API_H_
#define SYSTEM_LIBC_FS_API_H_

#include <dirent.h>
#include <fnmatch.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/types.h>
#include <unistd.h>
#include <string>

#include "base/macros.h"
#include "util/scoped_cleanup.h"

namespace system_api {

// The default implementation forwards the functions here to stdio.h (the FILE *
// functions), sys/stat.h, unistd.h, dirent.h (OpenDir), sys/mount.h (the
// mount/umount functions), and sys/ioctl.h.
// For example, FOpen(...) -> ::fopen(...), FdOpen(...) -> ::fdopen(...).
class LibcFsApi {
 public:
  virtual ~LibcFsApi() {}

  virtual FILE *FOpen(const char *path, const char *mode) const = 0;

  virtual FILE *FdOpen(int file_descriptor, const char *mode) const = 0;

  // Redirects stream to the newly open file, closes previous stream if open.
  virtual FILE *FReopen(const char *path, const char *mode,
                        FILE *stream) const = 0;

  virtual DIR *OpenDir(const char *name) const = 0;

  // Returns a file descriptor. See fcntl.h for possible flag values.
  virtual int Open(const char *path, int oflag) const = 0;

  virtual int OpenWithMode(const char *path, int oflag, int mode) const = 0;

  // Returns 0 on success, EOF on failure.
  virtual int FClose(FILE *file_pointer) const = 0;

  // Expects to scan a file with the format "%u:%u". Forwards to fscanf.
  virtual int FScanfUU(FILE *file_pointer, unsigned int *first,
                       unsigned int *second) const = 0;

  // Functions below return 0 on success, -1 on failure.
  virtual int Close(int file_descriptor) const = 0;

  virtual int ChMod(const char *path, mode_t mode) const = 0;

  // -1 means "do not change".
  virtual int ChOwn(const char *path, uid_t owner, gid_t group) const = 0;

  virtual int LChOwn(const char *path, uid_t owner, gid_t group) const = 0;

  virtual int FChOwn(int fd, uid_t owner, gid_t group) const = 0;

  virtual int MkNod(const char *path, mode_t mode, dev_t dev) const = 0;

  virtual int Unlink(const char *path) const = 0;

  virtual int Rename(const char *oldpath, const char *newpath) const = 0;

  virtual int MkDir(const char *path, mode_t mode) const = 0;

  virtual int RmDir(const char *path) const = 0;

  virtual int Stat(const char *path, struct stat *buf) const = 0;

  virtual int Stat64(const char *path, struct stat64 *buf) const = 0;

  // For a link, LStat informs about link itself, Stat about referenced file.
  virtual int LStat(const char *path, struct stat *buf) const = 0;

  virtual int FStat(int file_descriptor, struct stat *buf) const = 0;

  virtual int StatFs64(const char *path, struct statfs64 *buf) const = 0;

  virtual int Mount(const char *source, const char *target,
                    const char *filesystemtype,
                    unsigned long mountflags,  // NOLINT
                    const void *data) const = 0;

  virtual int UMount(const char *target) const = 0;

  virtual int UMount2(const char *target, int flags) const = 0;

  // Read/Write at most nmemb elements of size size each.
  // Return number of elements (_not_ bytes) processed.
  virtual int FRead(void *ptr, size_t size, size_t nmemb,
                    FILE *stream) const = 0;

  virtual int FWrite(const void *ptr, size_t size, size_t nmemb,
                     FILE *stream) const = 0;

  // Get a newline-terminated string of finite length (at most n) from stream.
  virtual char *FGetS(char *buf, int n, FILE *stream) const = 0;

  // Checks if the error indicator associated with 'file' is set, returning a
  // value different from zero if it is.
  virtual int FError(FILE *stream) const = 0;

  // Read/Write at most nbytes bytes. Returns bytes read.
  virtual ssize_t Read(int file_descriptor, char *buf, size_t nbytes) const = 0;

  virtual ssize_t Write(int file_descriptor, const void *buf,
                        size_t nbytes) const = 0;

  virtual int FSync(int file_descriptor) const = 0;

  virtual int ChDir(const char *path) const = 0;

  virtual int ReadDirR(DIR *dir, dirent *entry, dirent **result) const = 0;

  virtual int CloseDir(DIR *dir) const = 0;

  virtual ssize_t ReadLink(const char *path, char *buf, size_t len) const = 0;

  virtual int SymLink(const char *from, const char *to) const = 0;

  virtual int Link(const char *from, const char *to) const = 0;

  virtual int Access(const char *name, int type) const = 0;

  // Dangerous as you cannot specify max length of |resolved|.
  virtual char *RealPath(const char *name, char *resolved) const = 0;

  virtual int FnMatch(const char *pattern, const char *str,
                      int flags) const = 0;

  virtual int Ioctl(int fd, int request, void *argp) const = 0;

  virtual int Pipe(int pipefd[2]) const = 0;

  virtual int Pipe2(int pipefd[2], int flags) const = 0;

  virtual int ChRoot(const char *path) const = 0;

  virtual int PivotRoot(const char *new_root, const char *put_old) const = 0;

  virtual int Dup2(int olfd, int newfd) const = 0;

  // fcntl() is a variable argument function. Its not possible to mock such
  // function. So we add overloads for this method as we need.
  virtual int FCntl(int fd, int cmd, int arg1) const = 0;

  // The following functions could be added here if needed:
  // - fseek
  // - ftell
  // - rewind
  // - fgetpos
  // - fsetpos
  // - fdopendir
  // - mmap

 protected:
  LibcFsApi() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(LibcFsApi);
};

// Returns a singleton instance of the LibcFsApi interface implementation.
const LibcFsApi *GlobalLibcFsApi();

// An RAII file-descriptor closer.
struct ScopedFileCloser : public util::ScopedCleanup {
  explicit ScopedFileCloser(int fd)
      : util::ScopedCleanup(&ScopedFileCloser::Close, fd) {}
  static void Close(int fd) { GlobalLibcFsApi()->Close(fd); }
};

// An RAII file unlinker.
struct ScopedFileUnlinker : public util::ScopedCleanup {
  explicit ScopedFileUnlinker(const ::std::string &path)
      : util::ScopedCleanup(&ScopedFileUnlinker::Unlink, path) {}
  static void Unlink(const ::std::string &path) {
    GlobalLibcFsApi()->Unlink(path.c_str());
  }
};

}  // namespace system_api

#endif  // SYSTEM_LIBC_FS_API_H_
