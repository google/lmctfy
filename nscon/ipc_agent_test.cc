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

#include "nscon/ipc_agent.h"

#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <memory>

#include "global_utils/time_utils_test_util.h"
#include "util/errors_test_util.h"
#include "system_api/libc_fs_api_test_util.h"
#include "system_api/libc_net_api_test_util.h"
#include "system_api/libc_process_api_test_util.h"
#include "gtest/gtest.h"

using ::std::pair;
using ::std::unique_ptr;
using ::testing::Invoke;
using ::testing::NotNull;
using ::testing::Return;
using ::testing::SetArrayArgument;
using ::testing::SetErrnoAndReturn;
using ::testing::StrEq;
using ::testing::_;
using ::util::error::INTERNAL;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace nscon {

static const int kSocket = 32;
static const int kPipefdRead = 88;
static const int kPipefdWrite = 99;
static const pid_t kPid = 650;
static const char kData[] = "hello 5353";
static const ::util::Microseconds kTime(1392567140);
static const char kPath[] = "/tmp/nscon.uds_650_1392567140";

MATCHER(HasUnixPath, "") {
  const struct sockaddr_un *addr = (const struct sockaddr_un *)arg;
  if (strcmp(addr->sun_path, kPath) != 0) {
    *result_listener << "Expected \"" << kPath << "\" but received \""
                     << addr->sun_path << "\"";
    return false;
  }

  return true;
}

class IpcAgentTest : public ::testing::Test {
 public:
  void SetUp() override {
    int pipefd[2] = { kPipefdRead, kPipefdWrite };
    ipc_agent_.reset(new IpcAgent(kSocket, kPath, pipefd));
  }

  void TearDown() override {
    EXPECT_CALL(libc_fs_api_.Mock(), Close(kSocket)).WillOnce(Return(0));
    EXPECT_CALL(libc_fs_api_.Mock(), Close(kPipefdRead))
        .WillRepeatedly(Return(0));
    EXPECT_CALL(libc_fs_api_.Mock(), Close(kPipefdWrite))
        .WillRepeatedly(Return(0));

    ipc_agent_.reset(NULL);  // Invokes destructor.
  }

  // Wrapper for protected functions in IpcAgent class.
  IpcAgent *NewIpcAgent(int sock_fd, const string &uds_path,
                        const int pipefd[2]) {
    return new IpcAgent(sock_fd, uds_path, pipefd);
  }

 protected:
  unique_ptr<IpcAgent> ipc_agent_;
  unique_ptr<IpcAgentFactory> ipc_agent_factory_;
  ::system_api::MockLibcFsApiOverride libc_fs_api_;
  ::system_api::MockLibcNetApiOverride libc_net_api_;
  ::system_api::MockLibcProcessApiOverride libc_process_api_;
  ::util::MockTimeUtilsOverride time_utils_;
};

typedef IpcAgentTest IpcAgentFactoryTest;

TEST_F(IpcAgentFactoryTest, Create) {
  // Implicitly tested by SetUp() and TearDown().
  unique_ptr<IpcAgentFactory> ipc_agent_factory(new IpcAgentFactory());
  EXPECT_CALL(libc_net_api_.Mock(),
              Socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0))
      .WillOnce(Return(kSocket));
  EXPECT_CALL(libc_net_api_.Mock(), Bind(kSocket, HasUnixPath(), _))
      .WillOnce(Return(0));
  EXPECT_CALL(libc_net_api_.Mock(), Listen(kSocket, 1))
      .WillOnce(Return(0));

  EXPECT_CALL(libc_process_api_.Mock(), GetPid())
      .WillRepeatedly(Return(kPid));
  EXPECT_CALL(time_utils_.Mock(), MicrosecondsSinceEpoch())
      .WillRepeatedly(Return(kTime));
  EXPECT_CALL(libc_fs_api_.Mock(), ChMod(StrEq(kPath), 0777))
      .WillOnce(Return(0));

  int pipefd[2] = { kPipefdRead, kPipefdWrite };
  EXPECT_CALL(libc_fs_api_.Mock(), Pipe2(NotNull(), O_CLOEXEC))
      .WillOnce(DoAll(SetArrayArgument<0>(pipefd, pipefd + 2), Return(0)));

  StatusOr<IpcAgent *> statusor = ipc_agent_factory->Create();
  ASSERT_OK(statusor);

  EXPECT_CALL(libc_fs_api_.Mock(), Close(kSocket)).WillOnce(Return(0));
  EXPECT_CALL(libc_fs_api_.Mock(), Close(kPipefdRead)).WillOnce(Return(0));
  EXPECT_CALL(libc_fs_api_.Mock(), Close(kPipefdWrite)).WillOnce(Return(0));
  delete statusor.ValueOrDie();
}

TEST_F(IpcAgentTest, Destroy) {
  int pipefd[2] = { kPipefdRead, kPipefdWrite };
  unique_ptr<IpcAgent> ipc_agent(NewIpcAgent(kSocket, kPath, pipefd));

  EXPECT_CALL(libc_fs_api_.Mock(), Unlink(StrEq(kPath))).WillOnce(Return(0));
  EXPECT_CALL(libc_fs_api_.Mock(), Close(kSocket)).WillOnce(Return(0));
  EXPECT_CALL(libc_fs_api_.Mock(), Close(kPipefdRead)).WillOnce(Return(0));
  EXPECT_CALL(libc_fs_api_.Mock(), Close(kPipefdWrite)).WillOnce(Return(0));

  ASSERT_OK(ipc_agent->Destroy());
  // Destroy() deletes the IPC agent so release it here.
  ipc_agent.release();
}

typedef IpcAgentTest WriteDataTest;

TEST_F(WriteDataTest, Success) {
  const int kWriteSocket = 42;

  EXPECT_CALL(libc_net_api_.Mock(), Socket(AF_UNIX, SOCK_STREAM, 0))
      .WillOnce(Return(kWriteSocket));
  EXPECT_CALL(libc_fs_api_.Mock(), Close(kWriteSocket)).WillOnce(Return(0));
  EXPECT_CALL(libc_net_api_.Mock(), Connect(kWriteSocket, HasUnixPath(), _))
      .WillOnce(Return(0));
  EXPECT_CALL(libc_net_api_.Mock(), Send(kWriteSocket, NotNull(), _, 0))
      .WillOnce(Return(0));

  EXPECT_OK(ipc_agent_->WriteData(kData));
}

TEST_F(WriteDataTest, SocketFailure) {
  EXPECT_CALL(libc_net_api_.Mock(), Socket(AF_UNIX, SOCK_STREAM, 0))
      .WillOnce(SetErrnoAndReturn(EINVAL, -1));

  EXPECT_ERROR_CODE(INTERNAL, ipc_agent_->WriteData(kData));
}

TEST_F(WriteDataTest, ConnectFailure) {
  const int kWriteSocket = 42;

  EXPECT_CALL(libc_net_api_.Mock(), Socket(AF_UNIX, SOCK_STREAM, 0))
      .WillOnce(Return(kWriteSocket));
  EXPECT_CALL(libc_fs_api_.Mock(), Close(kWriteSocket)).WillOnce(Return(0));
  EXPECT_CALL(libc_net_api_.Mock(), Connect(kWriteSocket, HasUnixPath(), _))
      .WillOnce(SetErrnoAndReturn(EBADF, -1));

  EXPECT_ERROR_CODE(INTERNAL, ipc_agent_->WriteData(kData));
}

TEST_F(WriteDataTest, SendFailure) {
  const int kWriteSocket = 42;

  EXPECT_CALL(libc_net_api_.Mock(), Socket(AF_UNIX, SOCK_STREAM, 0))
      .WillOnce(Return(kWriteSocket));
  EXPECT_CALL(libc_fs_api_.Mock(), Close(kWriteSocket)).WillOnce(Return(0));
  EXPECT_CALL(libc_net_api_.Mock(), Connect(kWriteSocket, HasUnixPath(), _))
      .WillOnce(Return(0));
  EXPECT_CALL(libc_net_api_.Mock(), Send(kWriteSocket, NotNull(), _, 0))
      .WillOnce(SetErrnoAndReturn(EINVAL, -1));

  EXPECT_ERROR_CODE(INTERNAL, ipc_agent_->WriteData(kData));
}

void SetPid(int sockfd, int level, int optname, void *optval,
            socklen_t *optlen) {
  static_cast<struct ucred *>(optval)->pid = kPid;
}

void SetRecvData(int sockfd, void *buf, size_t len, int flags) {
  strncpy(static_cast<char *>(buf), kData, len);
}

typedef IpcAgentTest ReadDataTest;

TEST_F(ReadDataTest, Success) {
  const int kReadSocket = 52;

  EXPECT_CALL(libc_net_api_.Mock(), Accept(kSocket, 0, 0))
      .WillOnce(Return(kReadSocket));
  EXPECT_CALL(
      libc_net_api_.Mock(),
      GetSockOpt(kReadSocket, SOL_SOCKET, SO_PEERCRED, NotNull(), NotNull()))
      .WillOnce(DoAll(Invoke(&SetPid), Return(0)));
  EXPECT_CALL(libc_net_api_.Mock(), Recv(kReadSocket, NotNull(), _, 0))
      .WillOnce(DoAll(Invoke(&SetRecvData), Return(0)));
  EXPECT_CALL(libc_fs_api_.Mock(), Close(kReadSocket))
      .WillOnce(Return(0));

  StatusOr<pair<string, pid_t>> statusor = ipc_agent_->ReadData();
  ASSERT_OK(statusor);
  pair<string, pid_t> result = statusor.ValueOrDie();
  EXPECT_EQ(kData, result.first);
  EXPECT_EQ(kPid, result.second);
}

TEST_F(ReadDataTest, AcceptFailure) {
  EXPECT_CALL(libc_net_api_.Mock(), Accept(kSocket, 0, 0))
      .WillOnce(SetErrnoAndReturn(EBADF, -1));

  EXPECT_ERROR_CODE(INTERNAL, ipc_agent_->ReadData());
}

TEST_F(ReadDataTest, GetSockOptFailure) {
  const int kReadSocket = 52;

  EXPECT_CALL(libc_net_api_.Mock(), Accept(kSocket, 0, 0))
      .WillOnce(Return(kReadSocket));
  EXPECT_CALL(libc_fs_api_.Mock(), Close(kReadSocket))
      .WillOnce(Return(0));
  EXPECT_CALL(
      libc_net_api_.Mock(),
      GetSockOpt(kReadSocket, SOL_SOCKET, SO_PEERCRED, NotNull(), NotNull()))
      .WillOnce(SetErrnoAndReturn(EBADF, -1));

  EXPECT_ERROR_CODE(INTERNAL, ipc_agent_->ReadData());
}

TEST_F(ReadDataTest, RecvFailure) {
  const int kReadSocket = 52;

  EXPECT_CALL(libc_net_api_.Mock(), Accept(kSocket, 0, 0))
      .WillOnce(Return(kReadSocket));
  EXPECT_CALL(libc_fs_api_.Mock(), Close(kReadSocket))
      .WillOnce(Return(0));
  EXPECT_CALL(
      libc_net_api_.Mock(),
      GetSockOpt(kReadSocket, SOL_SOCKET, SO_PEERCRED, NotNull(), NotNull()))
      .WillOnce(DoAll(Invoke(&SetPid), Return(0)));
  EXPECT_CALL(libc_net_api_.Mock(), Recv(kReadSocket, NotNull(), _, 0))
      .WillOnce(SetErrnoAndReturn(EBADF, -1));

  EXPECT_ERROR_CODE(INTERNAL, ipc_agent_->ReadData());
}

typedef IpcAgentTest WaitForChildTest;

TEST_F(WaitForChildTest, Ok) {
  EXPECT_CALL(libc_fs_api_.Mock(), Close(kPipefdWrite)).WillOnce(Return(0));
  EXPECT_CALL(libc_fs_api_.Mock(), Read(kPipefdRead, NotNull(), 1))
      .WillOnce(Return(1));

  EXPECT_OK(ipc_agent_->WaitForChild());
}

TEST_F(WaitForChildTest, Cancelled) {
  EXPECT_CALL(libc_fs_api_.Mock(), Close(kPipefdWrite)).WillOnce(Return(0));
  EXPECT_CALL(libc_fs_api_.Mock(), Read(kPipefdRead, NotNull(), 1))
      .WillOnce(Return(0));

  EXPECT_ERROR_CODE(::util::error::CANCELLED, ipc_agent_->WaitForChild());
}

TEST_F(WaitForChildTest, ReadFailure) {
  EXPECT_CALL(libc_fs_api_.Mock(), Close(kPipefdWrite)).WillOnce(Return(0));
  EXPECT_CALL(libc_fs_api_.Mock(), Read(kPipefdRead, NotNull(), 1))
      .WillOnce(SetErrnoAndReturn(EBADF, -1));

  EXPECT_ERROR_CODE(INTERNAL, ipc_agent_->WaitForChild());
}

typedef IpcAgentTest SignalParentTest;

TEST_F(SignalParentTest, Ok) {
  EXPECT_CALL(libc_fs_api_.Mock(), Close(kPipefdRead)).WillOnce(Return(0));
  EXPECT_CALL(libc_fs_api_.Mock(), Write(kPipefdWrite, NotNull(), 1))
      .WillOnce(Return(1));

  EXPECT_OK(ipc_agent_->SignalParent());
}

TEST_F(SignalParentTest, WriteFailure) {
  EXPECT_CALL(libc_fs_api_.Mock(), Close(kPipefdRead)).WillOnce(Return(0));
  EXPECT_CALL(libc_fs_api_.Mock(), Write(kPipefdWrite, NotNull(), 1))
      .WillOnce(SetErrnoAndReturn(EBADF, -1));

  EXPECT_ERROR_CODE(INTERNAL, ipc_agent_->SignalParent());
}

// TODO(vmarmol): Add integration tests for IpcAgent.

}  // namespace nscon
}  // namespace containers
