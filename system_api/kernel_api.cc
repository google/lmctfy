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

#include "system_api/kernel_api.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/file.h>
#include <sys/klog.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/swap.h>
#include <unistd.h>
#include <algorithm>
#include <memory>

#include "base/integral_types.h"
#include "base/logging.h"
#include "base/sysinfo.h"
#include "base/timer.h"
#include "file/base/helpers.h"
#include "strings/strip.h"
#include "strings/substitute.h"

using ::std::string;
using ::std::vector;
using ::strings::Substitute;

namespace system_api {

namespace {

bool Exists(const string &file_name) {
  struct stat unused;
  return stat(file_name.c_str(), &unused) == 0;
}

bool IsDirectory(const string &file_name) {
  struct stat s;
  return (stat(file_name.c_str(), &s) == 0) && S_ISDIR(s.st_mode);
}

bool ReadFileToStringHelper(const string &file_name, string *output) {
  int fd = open(file_name.c_str(), O_RDONLY);
  if (fd < 0) {
    return false;
  }

  int64 kBufSize = 1 << 20;  // 1MB
  char *buf = new char[kBufSize];
  int64 nread = 0;

  // Read until there are no more bytes.
  while ((nread = read(fd, buf, kBufSize)) > 0) {
    output->append(buf, nread);
  }

  delete[] buf;

  int ret = close(fd);
  return nread >= 0 && ret == 0;
}

}  // namesapce

int KernelAPI::MkDir(const string& path) const {
  ElapsedTimer timer("MkDir", true, kMaxAllowedTimeInSec);
  return mkdir(path.c_str(), 0755);
}

int KernelAPI::MkDirRecursive(const string& path) const {
  ElapsedTimer timer("MkDirRecursive", true, kMaxAllowedTimeInSec);

  string dir_path = path;

  // Strip trailing "/"
  if (dir_path.back() == '/') {
    dir_path.pop_back();
  }

  // Find the first directory that already exists, starting from the specified
  // path.
  const size_t kOne = 1;
  while (!IsDirectory(dir_path)) {
    dir_path = dir_path.substr(0, ::std::max(kOne, dir_path.find_last_of('/')));
  }

  // Make all necessary directories. Build them in reverse starting with the
  // first directory that did not already exist.
  while (dir_path.length() != path.length()) {
    // Path contains the full path, so we grab one more path component than what
    // has already been created. This is one char more than the length of the
    // existing.
    dir_path = path.substr(0, path.find('/', dir_path.length() + 1));
    int ret = mkdir(dir_path.c_str(), 0755);
    if (ret != 0) {
      return ret;
    }
  }

  return 0;
}

int KernelAPI::RmDir(const string& path) const {
  ElapsedTimer timer("RmDir", true, kMaxAllowedTimeInSec);
  const int kNumRetries = 3;
  int retval = 0;
  for (int i = 0; i < kNumRetries; ++i) {
    retval = rmdir(path.c_str());
    if (retval == -1 && errno == EINTR) {
      continue;
    }
    break;
  }
  return retval;
}

int KernelAPI::Kill(pid_t pid) const {
  ElapsedTimer timer("Kill", true, kMaxAllowedTimeInSec);
  return kill(pid, SIGKILL);
}

int KernelAPI::Signal(pid_t pid, int sig) const {
  ElapsedTimer timer("Signal", true, kMaxAllowedTimeInSec);
  return kill(pid, sig);
}

int KernelAPI::PthreadKill(pthread_t thread, int sig) const {
  ElapsedTimer timer("PthreadKill", true, kMaxAllowedTimeInSec);
  return pthread_kill(thread, sig);
}

int KernelAPI::SwapOn(const string& path, int64 flags) const {
  ElapsedTimer timer("SwapOn", true, kMaxAllowedTimeInSec);
  return swapon(path.c_str(), flags);
}

int KernelAPI::SwapOff(const string& path) const {
  ElapsedTimer timer("SwapOff", true, kMaxAllowedTimeInSec);
  return swapoff(path.c_str());
}

int KernelAPI::SchedSetAffinity(
    pid_t pid,
    const cpu_set_t *cpu_set) const {
  ElapsedTimer timer("SchedSetAffinity", true, kMaxAllowedTimeInSec);
  return sched_setaffinity(pid, CPU_SETSIZE, cpu_set);
}

pid_t KernelAPI::GetTID() const {
  ElapsedTimer timer("GetTID", true, kMaxAllowedTimeInSec);
  return ::GetTID();
}

// Get the current time.
time_t KernelAPI::Now() const { return time(NULL); }

bool KernelAPI::FileExists(const string &file_name) const {
  return ProcFileExists(file_name);
}

int KernelAPI::Access(const string &file_name, int mode) const {
  string debug = Substitute("Access $0", file_name);
  ElapsedTimer timer(debug.c_str(), true, kMaxAllowedTimeInSec);
  return access(file_name.c_str(), mode);
}

bool KernelAPI::ProcFileExists(const string &file_name) const {
  string debug = Substitute("ProcFileExists $0", file_name);
  ElapsedTimer timer(debug.c_str(), true, kMaxAllowedTimeInSec);
  return Exists(file_name);
}

bool KernelAPI::ReadFileToString(const string &file_name,
                                 string *output) const {
  string debug = Substitute("ReadFileToString: $0", file_name);
  ElapsedTimer timer(debug.c_str(), true, kMaxAllowedTimeInSec);
  return ReadFileToStringHelper(file_name, output);
}

::util::Status KernelAPI::GetFileContents(const string &file_name,
                                          string *output) const {
  string debug = Substitute("GetFileContents: $0", file_name);
  ElapsedTimer timer(debug.c_str(), true, kMaxAllowedTimeInSec);
  return ::file::GetContents(file_name, output, ::file::Defaults());
}

size_t KernelAPI::WriteResFile(const string& contents,
                               const string& path) const {
  return WriteResFileWithLog(contents, path, true);
}


size_t KernelAPI::WriteResFileWithLog(const string& contents,
                                      const string& path, bool log) const {
  if (log) {
    LOG(INFO) << "Writing '" << contents << "' to " << path;
  }
  return WriteResFileQuietOrDie(contents, path);
}

size_t KernelAPI::WriteResFileQuietOrDie(const string& contents,
                                         const string& path) const {
  string debug = "WriteResFileQuietOrDie: " + path;
  ElapsedTimer timer(debug.c_str(), true, kMaxAllowedTimeInSec);
  return WriteResFileQuietWithoutTimerOrDie(contents, path);
}

size_t KernelAPI::WriteResFileQuietWithoutTimerOrDie(
    const string& contents, const string& path) const {
  int fd;
  CHECK((fd = open(path.c_str(), O_WRONLY)) >= 0) << "Failed to open " << path;
  size_t bytes = write(fd, contents.c_str(), contents.size());
  int errno_tmp = errno;
  CHECK(close(fd) == 0) << "Failed to close " << path;
  errno = errno_tmp;
  return bytes;
}

size_t KernelAPI::SafeWriteResFileWithRetry(int retries, const string &contents,
                                            const string &path,
                                            bool *open_error,
                                            bool *write_error) const {
  string debug;
  debug =
      Substitute("SafeWriteResFileWithRetry: $0 retries: $1", path, retries);
  ElapsedTimer timer(debug.c_str(), true, kMaxAllowedTimeInSec);
  size_t retval = 0;
  for (int i = 0; i < retries; i++) {
    retval = SafeWriteResFile(contents, path, open_error, write_error);
    // retry errors with EINTR
    if (*write_error && retval == -1 && errno == EINTR) {
        continue;
    } else {
      // for success and other errors return
      break;
    }
  }
  return retval;
}

size_t KernelAPI::SafeWriteResFile(const string &contents, const string &path,
                                   bool *open_error, bool *write_error) const {
  string debug = Substitute("SafeWriteResFile: $0", path);
  ElapsedTimer timer(debug.c_str(), true, kMaxAllowedTimeInSec);

  return SafeWriteResFileWithoutTimer(
    contents, path, open_error, write_error);
}

size_t KernelAPI::SafeWriteResFileWithoutTimer(
                  const string &contents, const string &path,
                  bool *open_error, bool *write_error) const {
  *open_error = false;
  *write_error = false;
  int fd = open(path.c_str(), O_WRONLY);
  if (fd == -1) {
    *open_error = true;
    return -1;
  }
  int err = write(fd, contents.c_str(), contents.size());
  int errno_tmp = errno;
  if (err == -1) *write_error = true;
  close(fd);
  if (err == -1) errno = errno_tmp;
  return err;
}

void KernelAPI::WriteResFileWithLogOrDie(const string& contents,
                                         const string& path, bool log) const {
  LOG_IF(FATAL, WriteResFileWithLog(contents, path, log) == -1)
      << "Couldn't write to " << path;
}

void KernelAPI::WriteResFileOrDie(const string& contents,
                                  const string& path) const {
  WriteResFileWithLogOrDie(contents, path, true);
}

void KernelAPI::WriteResFileOrDieQuiet(const string& contents,
                                       const string& path) const {
  LOG_IF(FATAL, WriteResFileQuietOrDie(contents, path) == -1)
      << "Couldn't write '" << contents << "' to " << path;
}

size_t KernelAPI::WriteResFileWithRetry(int retries,
                                        const string &data,
                                        const string &file) const {
  size_t retval = 0;
  for (int i = 0; i < retries; i++) {
    retval = WriteResFile(data, file);
    // retry errors with EINTR
    if (retval == -1 && errno == EINTR) {
        continue;
    } else {
      // for other errors or success return
      break;
    }
  }
  if (retval == -1)
    LOG(ERROR) << "Writing " << data << " to " << file << " failed";
  return retval;
}

int KernelAPI::Eventfd(unsigned int initval, int flags) const {
  ElapsedTimer timer("Eventfd: ", true, kMaxAllowedTimeInSec);
  return eventfd(initval, flags);
}

int KernelAPI::EpollCreate(int size) const {
  ElapsedTimer timer("EpollCtl: ", true, kMaxAllowedTimeInSec);
  return epoll_create(size);
}

int KernelAPI::EpollCtl(int epfd, int op, int fd,
                        struct epoll_event *event) const {
  ElapsedTimer timer("EpollCtl: ", true, kMaxAllowedTimeInSec);
  return epoll_ctl(epfd, op, fd, event);
}

int KernelAPI::EpollWait(int epfd, struct epoll_event *events, int maxevents,
                         int timeout) const {
  ElapsedTimer timer("EpollWait: ", true, kMaxAllowedTimeInSec);
  return epoll_wait(epfd, events, maxevents, timeout);
}

ssize_t KernelAPI::Read(int fd, void *buf, int count) const {
  ElapsedTimer timer("Read: ", true, kMaxAllowedTimeInSec);
  return read(fd, buf, count);
}

int KernelAPI::Open(const char *pathname, int flags) const {
  ElapsedTimer timer("Open: ", true, kMaxAllowedTimeInSec);
  return open(pathname, flags);
}

int KernelAPI::OpenWithMode(const char *pathname, int flags,
                            mode_t mode) const {
  ElapsedTimer timer("Open: ", true, kMaxAllowedTimeInSec);
  return open(pathname, flags, mode);
}

int KernelAPI::Close(int fd) const {
  ElapsedTimer timer("Close: ", true, kMaxAllowedTimeInSec);
  return close(fd);
}

int KernelAPI::Unlink(const char *pathname) const {
  ElapsedTimer timer("Unlink: ", true, kMaxAllowedTimeInSec);
  return unlink(pathname);
}

int KernelAPI::Flock(int fd, int operation) const {
  ElapsedTimer timer("Flock: ", true, kMaxAllowedTimeInSec);
  return flock(fd, operation);
}

int KernelAPI::Chown(const string &path, uid_t owner, gid_t group) const {
  ElapsedTimer timer("Chown: ", true, kMaxAllowedTimeInSec);
  return chown(path.c_str(), owner, group);
}

int KernelAPI::Usleep(useconds_t usec) const {
  ElapsedTimer timer("Usleep: ", true, kMaxAllowedTimeInSec);
  return usleep(usec);
}

int KernelAPI::Execvp(const string &file, const vector<string> &argv) const {
  // Build a vector of C-compatible strings.
  vector<const char *> cargv;
  for (const string &s : argv) {
    cargv.push_back(s.c_str());
  }
  cargv.push_back(nullptr);

  return execvp(file.c_str(), const_cast<char *const *>(&cargv.front()));
}

int KernelAPI::SetITimer(int which, const struct itimerval *new_value,
                         struct itimerval *old_value) const {
  ElapsedTimer timer("SetITimer: ", true, kMaxAllowedTimeInSec);
  return setitimer(which, new_value, old_value);
}

int KernelAPI::Umount(const string& path) const {
  string debug = Substitute("Umount: $0", path);
  ElapsedTimer timer(debug.c_str(), true, kMaxAllowedTimeInSec);
  return umount(path.c_str());
}

int KernelAPI::Mount(const string& name, const string& path,
                     const string& fstype, uint64 flags,
                     const void *data) const {
  string debug = Substitute("Mount: $0", path);
  ElapsedTimer timer(debug.c_str(), true, kMaxAllowedTimeInSec);
  return mount(name.c_str(),
               path.c_str(),
               fstype.c_str(),
               static_cast<unsigned long>(flags),  // NOLINT(runtime/int)
               data);
}

}  // namespace system_api
