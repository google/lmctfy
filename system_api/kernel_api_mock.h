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

// Mock class for KernelAPI. This provides a single test class
// for clients of KernelAPI and avoids profusion of KernelAPI mocks.

#ifndef SYSTEM_API_KERNEL_API_MOCK_H_
#define SYSTEM_API_KERNEL_API_MOCK_H_

#include <stddef.h>
#include <string>

#include "base/macros.h"
#include "system_api/kernel_api.h"
#include "gmock/gmock.h"

namespace system_api {

class KernelAPIMock : public KernelAPI {
 public:
  KernelAPIMock() {}
  virtual ~KernelAPIMock() {}

  MOCK_CONST_METHOD1(MkDir, int(const string& path));
  MOCK_CONST_METHOD1(MkDirRecursive, int(const string& path));
  MOCK_CONST_METHOD1(RmDir, int(const string& path));
  MOCK_CONST_METHOD1(Kill, int(pid_t pid));
  MOCK_CONST_METHOD2(Signal, int(pid_t pid, int sig));
  MOCK_CONST_METHOD2(PthreadKill, int(pthread_t thread, int sig));
  MOCK_CONST_METHOD2(SwapOn, int(const string& path, int64 flags));
  MOCK_CONST_METHOD1(SwapOff, int(const string& path));
  MOCK_CONST_METHOD2(SchedSetAffinity,
                     int(pid_t pid, const cpu_set_t *cpu_set));
  MOCK_CONST_METHOD0(GetTID, pid_t());
  MOCK_CONST_METHOD0(Now, time_t());
  MOCK_CONST_METHOD1(FileExists, bool(const string &file_name));
  MOCK_CONST_METHOD2(Access, int(const string &file_name, int mode));
  MOCK_CONST_METHOD1(ProcFileExists, bool(const string &file_name));
  MOCK_CONST_METHOD2(ReadFileToString,
                     bool(const string& file_name, string* output));
  MOCK_CONST_METHOD2(GetFileContents,
                     ::util::Status(const string &file_name, string *output));
  MOCK_CONST_METHOD3(WriteResFileWithLog, size_t(const string& contents,
                                                 const string& path,
                                                 bool log));
  MOCK_CONST_METHOD2(WriteResFile,
                     size_t(const string& contents, const string& path));
  MOCK_CONST_METHOD2(WriteResFileQuietOrDie,
                     size_t(const string& contents, const string& path));
  MOCK_CONST_METHOD2(WriteResFileQuietWithoutTimerOrDie,
                     size_t(const string& contents, const string& path));
  MOCK_CONST_METHOD2(WriteResFileOrDie,
                     void(const string& contents, const string& path));
  MOCK_CONST_METHOD3(WriteResFileWithLogOrDie, void(const string& contents,
                                                    const string& path,
                                                    bool log));
  MOCK_CONST_METHOD2(WriteResFileOrDieQuiet,
                     void(const string& contents, const string& path));
  MOCK_CONST_METHOD3(WriteResFileWithRetry, size_t(int retries,
                                                   const string &data,
                                                   const string &file));
  MOCK_CONST_METHOD2(Eventfd, int(unsigned int initval, int flags));
  MOCK_CONST_METHOD1(EpollCreate, int(int size));
  MOCK_CONST_METHOD4(EpollCtl, int(int epfd, int op, int fd,
                                   struct epoll_event *event));
  MOCK_CONST_METHOD4(EpollWait, int(int epfd, struct epoll_event *events,
                                    int maxevents, int timeout));
  MOCK_CONST_METHOD3(Read, ssize_t(int fd, void *buf, int count));
  MOCK_CONST_METHOD2(Open, int(const char *pathname, int flags));
  MOCK_CONST_METHOD3(OpenWithMode, int(const char *pathname,
                                       int flags, mode_t mode));
  MOCK_CONST_METHOD1(Close, int(int fd));
  MOCK_CONST_METHOD1(Unlink, int(const char *pathname));
  MOCK_CONST_METHOD2(Flock, int(int fd, int operation));
  MOCK_CONST_METHOD3(Chown, int(const string &path, uid_t uid, gid_t gid));
  MOCK_CONST_METHOD1(Usleep, int(useconds_t usec));
  MOCK_CONST_METHOD4(SafeWriteResFile, size_t(const string &contents,
                                              const string &path,
                                              bool *open_error,
                                              bool *write_error));
  MOCK_CONST_METHOD4(SafeWriteResFileWithoutTimer,
                     size_t(const string &contents,
                            const string &path,
                            bool *open_error,
                            bool *write_error));
  MOCK_CONST_METHOD5(SafeWriteResFileWithRetry, size_t(int retries,
                                                       const string &contents,
                                                       const string &path,
                                                       bool *open_error,
                                                       bool *write_error));
  MOCK_CONST_METHOD1(Umount, int(const string& path));
  MOCK_CONST_METHOD5(Mount, int(const string& name, const string& path,
                                const string& fstype, uint64 flags,
                                const void *data));
  MOCK_CONST_METHOD2(Execvp, int(const string &filename,
                                 const ::std::vector<string> &argv));
  MOCK_CONST_METHOD3(SetITimer,
                     int(int which, const struct itimerval *new_value,
                         struct itimerval *old_value));

 private:
  DISALLOW_COPY_AND_ASSIGN(KernelAPIMock);
};

}  // namespace system_api

#endif  // SYSTEM_API_KERNEL_API_MOCK_H_
