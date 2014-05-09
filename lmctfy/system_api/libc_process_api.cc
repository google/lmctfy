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

#include "system_api/libc_process_api.h"

#include <grp.h>
#include <sched.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

namespace system_api {

namespace {

// The "real" implementation of the API.
class LibcProcessApiImpl : public LibcProcessApi {
 public:
  LibcProcessApiImpl() {}

  int GetRLimit(int resource, struct rlimit *rlim) const override {
    return ::getrlimit(resource, rlim);
  }

  int SetRLimit(int resource, const struct rlimit *rlim) const override {
    return ::setrlimit(resource, rlim);
  }

  int Kill(pid_t pid, int signal) const override {
    return ::kill(pid, signal);
  }

  pid_t Fork() const override {
    return ::fork();
  }

  int Clone(int (*fn)(void *), void *child_stack, int flags,  // NOLINT
            void *arg) const override {
    return ::clone(fn, child_stack, flags, arg);
  }

  int Execve(const char *filename, char *const argv[],
             char *const envp[]) const override {
    return ::execve(filename, argv, envp);
  }

  void _Exit(int status) const override {
    ::_exit(status);
  }

  int Unshare(int flags) const override {
    return ::unshare(flags);
  }

  int Setns(int fd, int nstype) const override {
    return ::setns(fd, nstype);
  }

  pid_t SetSid() const override {
    return ::setsid();
  }

  pid_t Wait(int *status) const override {
    return ::wait(status);
  }

  pid_t WaitPid(pid_t pid, int *status, int options) const override {
    return ::waitpid(pid, status, options);
  }

  int WaitId(idtype_t idtype, id_t id,
             siginfo_t *child_process_info,
             int options) const override {
    return ::waitid(idtype, id, child_process_info, options);
  }

  uid_t GetUid() const override {
    return ::getuid();
  }

  pid_t GetPid() const override {
    return ::getpid();
  }

  pid_t GetPGid(pid_t pid) const override {
    return ::getpgid(pid);
  }

  int SetResUid(uid_t ruid, uid_t euid, uid_t suid) const override {
    return ::setresuid(ruid, euid, suid);
  }

  int SetResGid(gid_t rgid, gid_t egid, gid_t sgid) const override {
    return ::setresgid(rgid, egid, sgid);
  }

  int SetGroups(size_t size, const gid_t *list) const override {
    return ::setgroups(size, list);
  }
};

}  // namespace

const LibcProcessApi *GlobalLibcProcessApi() {
  static LibcProcessApi *api = new LibcProcessApiImpl();
  return api;
}

}  // namespace system_api
