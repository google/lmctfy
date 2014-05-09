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

// The point of this is to enable injecting and mocking out filesystem
// interaction in unit tests. We define methods that (in production code)
// forward to the standard functions defined in stdio.h, but can be overridden
// using the tools provided in libcfs_test_util.h for testing.
//
// Author: kyurtsever@google.com (Kamil Yurtsever)

#ifndef SYSTEM_API_LIBC_PROCESS_API_H_
#define SYSTEM_API_LIBC_PROCESS_API_H_

#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "base/macros.h"

namespace system_api {

class LibcProcessApi {
 public:
  virtual ~LibcProcessApi() {}
  // Add other process managment functions here if needed.
  virtual int GetRLimit(int resource, struct rlimit *rlim) const = 0;
  virtual int SetRLimit(int resource, const struct rlimit *rlim) const = 0;
  virtual int Kill(pid_t pid, int signal) const = 0;
  virtual pid_t Fork() const = 0;
  virtual int Clone(int (*fn)(void *), void *child_stack, int flags,  // NOLINT
                    void *arg) const = 0;
  virtual int Execve(const char *filename, char *const argv[],
                     char *const envp[]) const = 0;
  virtual void _Exit(int status) const = 0;
  virtual int Unshare(int flags) const = 0;
  virtual int Setns(int fd, int nstype) const = 0;
  virtual pid_t SetSid() const = 0;

  // If child has been successfuly waited, then Wait and WaitPid return its pid.
  // If |status| pointer wasn't null they also put opaque status information
  // in the int pointed by |status|. Subsequently |status| can be inspected
  // with appropriate macros.
  virtual pid_t Wait(int *status) const = 0;
  virtual pid_t WaitPid(pid_t pid, int *status, int options) const = 0;
  virtual int WaitId(idtype_t idtype, id_t id, siginfo_t *child_process_info,
                     int options) const = 0;
  virtual uid_t GetUid() const = 0;
  virtual pid_t GetPid() const = 0;
  virtual pid_t GetPGid(pid_t pid) const = 0;
  virtual int SetResUid(uid_t ruid, uid_t euid, uid_t suid) const = 0;
  virtual int SetResGid(gid_t rgid, gid_t egid, gid_t sgid) const = 0;
  virtual int SetGroups(size_t size, const gid_t *list) const = 0;

 protected:
  LibcProcessApi() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(LibcProcessApi);
};

// Returns a singleton instance of the LibcProcessApi interface implementation.
const LibcProcessApi *GlobalLibcProcessApi();

}  // namespace system_api

#endif  // SYSTEM_API_LIBC_PROCESS_API_H_
