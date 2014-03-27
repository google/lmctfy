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

// Unit-testing utilities for LibcNetApi module

#ifndef SYSTEM_API_LIBC_NET_API_TEST_UTIL_H_
#define SYSTEM_API_LIBC_NET_API_TEST_UTIL_H_

#include "system_api/libc_net_api.h"
#include "gmock/gmock.h"

namespace system_api {

class MockLibcNetApi : public LibcNetApi {
 public:
  MOCK_CONST_METHOD3(Accept, int(int sockfd, struct sockaddr *addr,
                                 socklen_t *addrlen));
  MOCK_CONST_METHOD3(Bind, int(int sockfd, const struct sockaddr *addr,
                               socklen_t addrlen));
  MOCK_CONST_METHOD3(Connect, int(int sockfd, const struct sockaddr *addr,
                                  socklen_t addrlen));
  MOCK_CONST_METHOD5(GetSockOpt, int(int sockfd, int level, int optname,
                                     void *optval, socklen_t *optlen));
  MOCK_CONST_METHOD2(Listen, int(int sockfd, int backlog));
  MOCK_CONST_METHOD4(Recv,
                     ssize_t(int sockfd, void *buf, size_t len, int flags));
  MOCK_CONST_METHOD4(Send, ssize_t(int sockfd, const void *buf, size_t len,
                                   int flags));
  MOCK_CONST_METHOD2(SetHostname, int(const char *, size_t));
  MOCK_CONST_METHOD5(SetSockOpt, int(int sockfd, int level, int optname,
                                     const void *optval, socklen_t optlen));
  MOCK_CONST_METHOD3(Socket, int(int domain, int type, int protocol));
};

extern const LibcNetApi *GlobalLibcNetApi();

class MockLibcNetApiOverride {
 public:
  ::testing::StrictMock<MockLibcNetApi> &Mock() {
    static ::testing::StrictMock<MockLibcNetApi> *ptr =
        (::testing::StrictMock<MockLibcNetApi> *)GlobalLibcNetApi();
    return *ptr;
  }
};

}  // namespace system_api

#endif  // SYSTEM_API_LIBC_NET_API_TEST_UTIL_H_
