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
//
// Unit testing utilities for LibcProcessApi module.

#ifndef SYSTEM_API_LIBC_PROCESS_API_TEST_UTIL_H_
#define SYSTEM_API_LIBC_PROCESS_API_TEST_UTIL_H_

#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "system_api/libc_process_api.h"
#include "gmock/gmock.h"

namespace system_api {

class MockLibcProcessApi : public LibcProcessApi {
 public:
  MOCK_CONST_METHOD2(GetRLimit, int(int resource, struct rlimit *rlim));
  MOCK_CONST_METHOD2(SetRLimit, int(int resource, const struct rlimit *rlim));
  MOCK_CONST_METHOD2(Kill, int(pid_t pid, int signal));
  MOCK_CONST_METHOD0(Fork, pid_t(void));
  MOCK_CONST_METHOD4(Clone, int(int (*fn)(void *), void *child_stack,  // NOLINT
                                int flags, void *arg));
  MOCK_CONST_METHOD3(Execve, int(const char *filename, char *const argv[],
                                 char *const envp[]));
  MOCK_CONST_METHOD1(_Exit, void(int status));
  MOCK_CONST_METHOD1(Unshare, int(int flags));
  MOCK_CONST_METHOD2(Setns, int(int fd, int nstype));
  MOCK_CONST_METHOD0(SetSid, pid_t());
  MOCK_CONST_METHOD1(Wait, pid_t(int *status));
  MOCK_CONST_METHOD3(WaitPid, pid_t(pid_t pid, int *status, int options));
  MOCK_CONST_METHOD4(WaitId, int(idtype_t idtype, id_t id,
                                 siginfo_t *child_process_info, int options));
  MOCK_CONST_METHOD0(GetUid, uid_t());
  MOCK_CONST_METHOD0(GetPid, pid_t());
  MOCK_CONST_METHOD1(GetPGid, pid_t(pid_t));
  MOCK_CONST_METHOD3(SetResUid, int(uid_t ruid, uid_t euid, uid_t suid));
  MOCK_CONST_METHOD3(SetResGid, int(gid_t rgid, gid_t egid, gid_t sgid));
  MOCK_CONST_METHOD2(SetGroups, int(size_t size, const gid_t *list));
};

extern const LibcProcessApi *GlobalLibcProcessApi();

class MockLibcProcessApiOverride {
 public:
  ::testing::StrictMock<MockLibcProcessApi> &Mock() {
    static ::testing::StrictMock<MockLibcProcessApi> *ptr =
        (::testing::StrictMock<MockLibcProcessApi> *) GlobalLibcProcessApi();
    return *ptr;
  }
};

}  // namespace system_api

#endif  // SYSTEM_API_LIBC_PROCESS_API_TEST_UTIL_H_
