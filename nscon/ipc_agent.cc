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

//
// IpcAgent implementation that uses a pipe to transfer data.
//
#include "nscon/ipc_agent.h"

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <fcntl.h>

#include "file/base/path.h"
#include "global_utils/time_utils.h"
#include "util/errors.h"
#include "system_api/libc_fs_api.h"
#include "system_api/libc_net_api.h"
#include "system_api/libc_process_api.h"
#include "strings/substitute.h"

using ::util::GlobalTimeUtils;
using ::system_api::GlobalLibcFsApi;
using ::system_api::GlobalLibcNetApi;
using ::system_api::GlobalLibcProcessApi;
using ::system_api::ScopedFileCloser;
using ::std::pair;
using ::std::unique_ptr;
using ::strings::Substitute;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace nscon {

class IpcAgentImpl : public IpcAgent {
 public:
  IpcAgentImpl(int sock_fd, const string &uds_path)
      : sock_fd_(sock_fd), uds_path_(uds_path) {}
  ~IpcAgentImpl() override;

  Status WriteData(const string &data) override;
  StatusOr<pair<string, pid_t>> ReadData() override;
  Status Destroy() override;

 private:
  // Unix-Domain Socket FD used for listening and accepting a connection by
  // receiver (during ReadData()).
  const int sock_fd_;
  // Unix-Domain Socket path used for connection by sender (during WriteData()).
  const string uds_path_;

  friend class IpcAgentFactory;

  DISALLOW_COPY_AND_ASSIGN(IpcAgentImpl);
};

static Status InitSockAddr(struct sockaddr_un *addr, const string &sun_path) {
  if (sun_path.length() >= sizeof(addr->sun_path)) {
    return Status(::util::error::INTERNAL, "unix-domain-socket path too long");
  }

  memset(addr, 0, sizeof(struct sockaddr_un));
  addr->sun_family = AF_UNIX;
  snprintf(addr->sun_path, sizeof(addr->sun_path), "%s", sun_path.c_str());

  return Status::OK;
}

// Returns the implementation of the IpcAgent class.
StatusOr<IpcAgent *> IpcAgentFactory::Create() const {
  int sock_fd =
      GlobalLibcNetApi()->Socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (sock_fd < 0) {
    return Status(::util::error::INTERNAL,
                  Substitute("socket() failed: $0", strerror(errno)));
  }

  // Auto-close the valid fd in case of errors.
  ScopedFileCloser fd_closer(sock_fd);

  const string uds_path = ::file::JoinPath(
      "/tmp/", Substitute("nscon.uds_$0_$1", GlobalLibcProcessApi()->GetPid(),
                          GlobalTimeUtils()->MicrosecondsSinceEpoch().value()));
  struct sockaddr_un addr;
  RETURN_IF_ERROR(InitSockAddr(&addr, uds_path));
  if (GlobalLibcNetApi()->Bind(sock_fd, (struct sockaddr *)&addr,
                               sizeof(addr)) < 0) {
    return Status(::util::error::INTERNAL,
                  Substitute("bind() failed: $0", strerror(errno)));
  }

  if (GlobalLibcNetApi()->Listen(sock_fd, 1) < 0) {
    return Status(::util::error::INTERNAL,
                  Substitute("listen() failed: $0", strerror(errno)));
  }

  fd_closer.Cancel();
  return new IpcAgentImpl(sock_fd, uds_path);
}

IpcAgentImpl::~IpcAgentImpl() {
  GlobalLibcFsApi()->Close(sock_fd_);
}

Status IpcAgentImpl::Destroy() {
  if (GlobalLibcFsApi()->Unlink(uds_path_.c_str()) < 0) {
    return Status(
        ::util::error::INTERNAL,
        Substitute("unlink($0) failed: $1", uds_path_, strerror(errno)));
  }

  delete this;
  return Status::OK;
}

// Creates a new socket and connects it to the unix-domain-path for
// communication. We need to take care that we call only basic system calls here
// as this gets invoked from between fork() and exec(). Using ScopedCleanup too
// resulted in a hang.
Status IpcAgentImpl::WriteData(const string &data) {
  int fd = GlobalLibcNetApi()->Socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    return Status(::util::error::INTERNAL,
                  Substitute("socket() failed: $0", strerror(errno)));
  }

  struct sockaddr_un addr;
  RETURN_IF_ERROR(InitSockAddr(&addr, uds_path_));

  if (GlobalLibcNetApi()->Connect(fd, (struct sockaddr *)&addr, sizeof(addr)) <
      0) {
    GlobalLibcFsApi()->Close(fd);
    return Status(::util::error::INTERNAL,
                  Substitute("connect() failed: $0", strerror(errno)));
  }

  ssize_t sent = GlobalLibcNetApi()->Send(fd, data.c_str(), data.size(), 0);
  if (sent < 0) {
    GlobalLibcFsApi()->Close(fd);
    return Status(::util::error::INTERNAL,
                  Substitute("send() failed: $0", strerror(errno)));
  }

  return Status::OK;
}

StatusOr<pair<string, pid_t>> IpcAgentImpl::ReadData() {
  int fd;
  do {
    fd = GlobalLibcNetApi()->Accept(sock_fd_, 0, 0);
    if (fd >= 0) {
      break;
    }
    if (errno == EINTR) {
      continue;
    }
    return Status(::util::error::INTERNAL,
                  Substitute("accept() failed: $0", strerror(errno)));
    // TODO(adityakali): Use SOCK_NONBLOCK above and wait for only few seconds.
  } while (1);

  // Auto-close the valid fd in case of errors.
  ScopedFileCloser fd_closer(fd);

  struct ucred credential;
  socklen_t len = sizeof(credential);
  if (GlobalLibcNetApi()->GetSockOpt(fd, SOL_SOCKET, SO_PEERCRED, &credential,
                                     &len) < 0) {
    return Status(::util::error::INTERNAL,
                  Substitute("getsockopt() failed: $0", strerror(errno)));
  }

  pid_t sender = credential.pid;
  // Read data from socket.
  char buf[4096];
  memset(buf, 0, sizeof(buf));
  ssize_t recvd = GlobalLibcNetApi()->Recv(fd, buf, sizeof(buf), 0);
  if (recvd < 0) {
    return Status(::util::error::INTERNAL,
                  Substitute("send() failed: $0", strerror(errno)));
  }

  return pair<string, pid_t>(buf, sender);
}

}  // namespace nscon
}  // namespace containers
