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

// The point of this is to enable injecting and mocking out networking
// interaction in unit tests. We define methods that (in production code)
// forward to the standard libc functions, but can be overridden
// using the tools provided in libc_net_api_test_util.h for testing.

#ifndef SYSTEM_API_LIBC_NET_API_H_
#define SYSTEM_API_LIBC_NET_API_H_

#include <sys/socket.h>

#include "base/macros.h"

namespace system_api {

class LibcNetApi {
 public:
  virtual ~LibcNetApi() {}

  virtual int Accept(int sockfd, struct sockaddr *addr,
                     socklen_t *addrlen) const = 0;
  virtual int Bind(int sockfd, const struct sockaddr *addr,
                   socklen_t addrlen) const = 0;
  virtual int Connect(int sockfd, const struct sockaddr *addr,
                      socklen_t addrlen) const = 0;
  virtual int GetSockOpt(int sockfd, int level, int optname, void *optval,
                         socklen_t *optlen) const = 0;
  virtual int Listen(int sockfd, int backlog) const = 0;
  virtual ssize_t Recv(int sockfd, void *buf, size_t len, int flags) const = 0;
  virtual ssize_t Send(int sockfd, const void *buf, size_t len,
                       int flags) const = 0;
  virtual int SetHostname(const char *name, size_t len) const = 0;
  virtual int SetSockOpt(int sockfd, int level, int optname, const void *optval,
                         socklen_t optlen) const = 0;
  virtual int Socket(int domain, int type, int protocol) const = 0;

 protected:
  LibcNetApi() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(LibcNetApi);
};

// Returns a singleton instance of the LibcNetApi interface implementation.
const LibcNetApi *GlobalLibcNetApi();

}  // namespace system_api

#endif  // SYSTEM_API_LIBC_NET_API_H_
