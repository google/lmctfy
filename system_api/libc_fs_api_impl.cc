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

#include "system_api/libc_fs_api_impl.h"

#include <dirent.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace system_api {

FILE *LibcFsApiImpl::FOpen(const char *path, const char *mode) const {
  return fopen(path, mode);
}

FILE *LibcFsApiImpl::FdOpen(int file_descriptor, const char *mode) const {
  return fdopen(file_descriptor, mode);
}

FILE *LibcFsApiImpl::FReopen(const char *path, const char *mode,
                             FILE *stream) const {
  return freopen(path, mode, stream);
}

DIR *LibcFsApiImpl::OpenDir(const char *name) const { return opendir(name); }

int LibcFsApiImpl::Open(const char *path, int oflag) const {
  return open(path, oflag);
}

int LibcFsApiImpl::OpenWithMode(const char *path, int oflag, int mode) const {
  return open(path, oflag, mode);
}

int LibcFsApiImpl::FClose(FILE *file_pointer) const {
  return fclose(file_pointer);
}

int LibcFsApiImpl::FScanfUU(FILE *file_pointer, unsigned int *first,
                            unsigned int *second) const {
  return fscanf(file_pointer, "%u:%u", first, second);
}

int LibcFsApiImpl::Close(int file_descriptor) const {
  return close(file_descriptor);
}

int LibcFsApiImpl::ChMod(const char *path, mode_t mode) const {
  return chmod(path, mode);
}

int LibcFsApiImpl::ChOwn(const char *path, uid_t owner, gid_t group) const {
  return chown(path, owner, group);
}

int LibcFsApiImpl::LChOwn(const char *path, uid_t owner, gid_t group) const {
  return lchown(path, owner, group);
}

int LibcFsApiImpl::FChOwn(int fd, uid_t owner, gid_t group) const {
  return fchown(fd, owner, group);
}

int LibcFsApiImpl::MkNod(const char *path, mode_t mode, dev_t dev) const {
  return mknod(path, mode, dev);
}

int LibcFsApiImpl::Unlink(const char *path) const { return unlink(path); }

int LibcFsApiImpl::Rename(const char *oldpath, const char *newpath) const {
  return rename(oldpath, newpath);
}

int LibcFsApiImpl::MkDir(const char *path, mode_t mode) const {
  return mkdir(path, mode);
}

int LibcFsApiImpl::RmDir(const char *path) const { return rmdir(path); }

int LibcFsApiImpl::Stat(const char *path, struct stat *buf) const {
  return stat(path, buf);
}

int LibcFsApiImpl::Stat64(const char *path, struct stat64 *buf) const {
  return stat64(path, buf);
}

int LibcFsApiImpl::LStat(const char *path, struct stat *buf) const {
  return lstat(path, buf);
}

int LibcFsApiImpl::FStat(int file_descriptor, struct stat *buf) const {
  return fstat(file_descriptor, buf);
}

int LibcFsApiImpl::StatFs64(const char *path, struct statfs64 *buf) const {
  return statfs64(path, buf);
}

int LibcFsApiImpl::Mount(const char *source, const char *target,
                         const char *filesystemtype,
                         unsigned long mountflags,  // NOLINT
                         const void *data) const {
  return mount(source, target, filesystemtype, mountflags, data);
}

int LibcFsApiImpl::UMount(const char *target) const { return umount(target); }

int LibcFsApiImpl::UMount2(const char *target, int flags) const {
  return umount2(target, flags);
}

int LibcFsApiImpl::FRead(void *ptr, size_t size, size_t nmemb,
                         FILE *stream) const {
  return fread(ptr, size, nmemb, stream);
}

int LibcFsApiImpl::FWrite(const void *ptr, size_t size, size_t nmemb,
                          FILE *stream) const {
  return fwrite(ptr, size, nmemb, stream);
}

char *LibcFsApiImpl::FGetS(char *buf, int n, FILE *stream) const {
  return fgets(buf, n, stream);
}

int LibcFsApiImpl::FError(FILE *stream) const { return ::ferror(stream); }

ssize_t LibcFsApiImpl::Read(int file_descriptor, char *buf,
                            size_t nbytes) const {
  return ::read(file_descriptor, reinterpret_cast<void *>(buf), nbytes);
}

ssize_t LibcFsApiImpl::Write(int file_descriptor, const void *buf,
                             size_t nbytes) const {
  return write(file_descriptor, buf, nbytes);
}

int LibcFsApiImpl::FSync(int file_descriptor) const {
  return fsync(file_descriptor);
}

int LibcFsApiImpl::ChDir(const char *path) const { return chdir(path); }

int LibcFsApiImpl::ReadDirR(DIR *dir, dirent *entry, dirent **result) const {
  return readdir_r(dir, entry, result);
}

int LibcFsApiImpl::CloseDir(DIR *dir) const { return closedir(dir); }

ssize_t LibcFsApiImpl::ReadLink(const char *path, char *buf, size_t len) const {
  ssize_t bytes_read = readlink(path, buf, len);
  if (bytes_read >= 0 && bytes_read < len) {
    buf[bytes_read] = '\0';
  }
  return bytes_read;
}

int LibcFsApiImpl::SymLink(const char *from, const char *to) const {
  return symlink(from, to);
}

int LibcFsApiImpl::Link(const char *from, const char *to) const {
  return link(from, to);
}

char *LibcFsApiImpl::RealPath(const char *name, char *resolved) const {
  return realpath(name, resolved);
}

int LibcFsApiImpl::Access(const char *name, int type) const {
  return access(name, type);
}

int LibcFsApiImpl::FnMatch(const char *pattern, const char *string,
                           int flags) const {
  return fnmatch(pattern, string, flags);
}

int LibcFsApiImpl::Ioctl(const int fd, const int request,
                         void *const argp) const {
  return ioctl(fd, request, argp);
}

int LibcFsApiImpl::Pipe(int pipefd[2]) const {
  return ::pipe(pipefd);
}

int LibcFsApiImpl::Pipe2(int pipefd[2], int flags) const {
  return pipe2(pipefd, flags);
}

int LibcFsApiImpl::ChRoot(const char *path) const {
  return chroot(path);
}

int LibcFsApiImpl::PivotRoot(const char *new_root, const char *put_old) const {
  return syscall(SYS_pivot_root, new_root, put_old);
}

int LibcFsApiImpl::Dup2(int oldfd, int newfd) const {
  return ::dup2(oldfd, newfd);
}

int LibcFsApiImpl::FCntl(int fd, int cmd, int arg1) const {
  return ::fcntl(fd, cmd, arg1);
}
}  // namespace system_api
