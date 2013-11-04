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

// Abstraction of various kernel features.
//
// Should possibly be merged with LibcAPI.

#ifndef SYSTEM_API_KERNEL_API_H_
#define SYSTEM_API_KERNEL_API_H_

#include <sched.h>
#include <stddef.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <map>
#include <string>
#include <vector>

#include "base/integral_types.h"
#include "base/logging.h"
#include "util/task/status.h"

struct pollfd;
struct epoll_event;

using ::std::string;

namespace system_api {

class SystemClockAPI {
 public:
  virtual time_t Now() const = 0;
  virtual ~SystemClockAPI() {}
};

class KernelAPI : public SystemClockAPI {
 public:
  KernelAPI()
      : sysbase_("/sys/") {}

  // From CpuSetContainer
  virtual ~KernelAPI() {}

  // Mockable functions that can be overridden in the unittests

  // Creates the specified directory with mode 0755
  virtual int MkDir(const string& path) const;

  // Recursively creates the specified directory with mode 0755.
  // Only creates the directories that don't already exist. If the directory
  // already exists, it is a no-op.
  virtual int MkDirRecursive(const string& path) const;
  // Removes a directory. Retries internally if the system call gets
  // interrupted.
  virtual int RmDir(const string& path) const;
  virtual int Kill(pid_t pid) const;
  virtual int Signal(pid_t pid, int sig) const;
  virtual int PthreadKill(pthread_t thread, int sig) const;
  virtual int SwapOn(const string& path, int64 flags) const;
  virtual int SwapOff(const string& path) const;
  virtual int SchedSetAffinity(
      pid_t pid,
      const cpu_set_t *cpu_set) const;
  virtual pid_t GetTID() const;

  // Get the current time.
  virtual time_t Now() const;

  virtual bool FileExists(const string &file_name) const;
  virtual int Access(const string &file_name, int mode) const;
  // This is distinct from ProcFileExists as this gets a fake procfs prefix in
  // integration tests.
  virtual bool ProcFileExists(const string &file_name) const;
  virtual bool ReadFileToString(const string &file_name, string *output) const;
  virtual ::util::Status GetFileContents(const string &file_name,
                                         string *output) const;
  virtual size_t WriteResFileWithLog(const string& contents,
                                    const string& path, bool log) const;
  virtual size_t WriteResFile(const string& contents,
                              const string& path) const;
  virtual size_t WriteResFileQuietOrDie(const string& contents,
                                        const string& path) const;
  virtual size_t WriteResFileQuietWithoutTimerOrDie(const string& contents,
                                                    const string& path) const;
  virtual void WriteResFileOrDie(const string& contents,
                                 const string& path) const;
  virtual void WriteResFileWithLogOrDie(const string& contents,
                                        const string& path, bool log) const;
  virtual void WriteResFileOrDieQuiet(const string& contents,
                                      const string& path) const;
  virtual size_t WriteResFileWithRetry(int retries, const string &data,
                                       const string &file) const;
  // Wrapper around eventfd() system call.
  virtual int Eventfd(unsigned int initval, int flags) const;
  // Wrapper around epoll_create() system call.
  virtual int EpollCreate(int size) const;
  // Wrapper around epoll_ctl() system call.
  virtual int EpollCtl(int epfd, int op, int fd,
                       struct epoll_event *event) const;
  // Wrapper around epoll_wait() system call.
  virtual int EpollWait(int epfd, struct epoll_event *events, int maxevents,
                        int timeout) const;
  // Wrapper around read() system call.
  virtual ssize_t Read(int fd, void *buf, int count) const;
  // Wrapper around open() system call.
  virtual int Open(const char *pathname, int flags) const;
  virtual int OpenWithMode(const char *pathname, int flags, mode_t mode) const;
  // Wrapper around close() system call.
  virtual int Close(int fd) const;
  // Wrapper around unlink() system call.
  virtual int Unlink(const char *pathname) const;
  // Wrapper around flock() system call.
  virtual int Flock(int fd, int operation) const;
  // Wrapper around chown() system call.
  virtual int Chown(const string &path, uid_t owner, gid_t group) const;
  // Wrapper around usleep() system call.
  virtual int Usleep(useconds_t usec) const;
  // A version which does no logging.
  virtual size_t SafeWriteResFile(const string &contents, const string &path,
                                  bool *open_error, bool *write_error) const;
  virtual size_t SafeWriteResFileWithoutTimer(const string &contents,
                                  const string &path, bool *open_error,
                                  bool *write_error) const;
  virtual size_t SafeWriteResFileWithRetry(int retries, const string &contents,
                                           const string &path, bool *open_error,
                                           bool *write_error) const;
  virtual int Execvp(const string &file,
                     const ::std::vector<string> &argv) const;
  virtual int SetITimer(int which, const struct itimerval *new_value,
                        struct itimerval *old_value) const;

  virtual int Umount(const string& path) const;
  virtual int Mount(const string& name, const string& path,
                    const string& fstype, uint64 flags,
                    const void *data) const;

  // Usually /sys, except in tests where it is e.g. $TEST_TMPDIR/sys
  virtual const string &SysBasePath() const { return sysbase_; }

  static const int32 kMaxAllowedTimeInSec = 1;

 private:
  const string sysbase_;
};

const KernelAPI *GlobalKernelApi();

}  // namespace system_api

#endif  // SYSTEM_API_KERNEL_API_H_
