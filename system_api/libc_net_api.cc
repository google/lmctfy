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

// LibcNetApi implementation

#include "system_api/libc_net_api.h"

#include <sys/types.h>
#include <unistd.h>

namespace system_api {
namespace {

class LibcNetApiImpl : public LibcNetApi {
 public:
  LibcNetApiImpl() {}

  int Accept(int sockfd, struct sockaddr *addr,
             socklen_t *addrlen) const override {
    return ::accept(sockfd, addr, addrlen);
  }
  int Bind(int sockfd, const struct sockaddr *addr,
           socklen_t addrlen) const override {
    return ::bind(sockfd, addr, addrlen);
  }
  int Connect(int sockfd, const struct sockaddr *addr,
              socklen_t addrlen) const override {
    return ::connect(sockfd, addr, addrlen);
  }
  int GetSockOpt(int sockfd, int level, int optname, void *optval,
                 socklen_t *optlen) const override {
    return ::getsockopt(sockfd, level, optname, optval, optlen);
  }
  int Listen(int sockfd, int backlog) const override {
    return ::listen(sockfd, backlog);
  }
  ssize_t Recv(int sockfd, void *buf, size_t len, int flags) const override {
    return ::recv(sockfd, buf, len, flags);
  }
  ssize_t Send(int sockfd, const void *buf, size_t len,
               int flags) const override {
    return ::send(sockfd, buf, len, flags);
  }
  int SetHostname(const char *name, size_t len) const override {
    return ::sethostname(name, len);
  }
  int SetSockOpt(int sockfd, int level, int optname, const void *optval,
                 socklen_t optlen) const override {
    return ::setsockopt(sockfd, level, optname, optval, optlen);
  }
  int Socket(int domain, int type, int protocol) const override {
    return ::socket(domain, type, protocol);
  }
};

}  // namespace

// The default singleton instantiation.
const LibcNetApi *GlobalLibcNetApi() {
  static LibcNetApi *api = new LibcNetApiImpl();
  return api;
}

}  // namespace system_api
