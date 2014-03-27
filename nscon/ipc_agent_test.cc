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
using ::testing::StrEq;
using ::testing::_;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace nscon {

static const int kSocket = 32;
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
  void SetUp() override {
    ipc_agent_factory_.reset(new IpcAgentFactory());

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

    StatusOr<IpcAgent *> statusor = ipc_agent_factory_->Create();
    ASSERT_OK(statusor);
    ipc_agent_.reset(statusor.ValueOrDie());
    EXPECT_NE(nullptr, ipc_agent_.get());
  }

  void TearDown() override {
    EXPECT_CALL(libc_fs_api_.Mock(), Unlink(StrEq(kPath))).WillOnce(Return(0));
    EXPECT_CALL(libc_fs_api_.Mock(), Close(kSocket)).WillOnce(Return(0));

    ASSERT_OK(ipc_agent_->Destroy());

    // Destroy() deletes the IPC agent so release it here.
    ipc_agent_.release();
  }

 protected:
  unique_ptr<IpcAgent> ipc_agent_;
  unique_ptr<IpcAgentFactory> ipc_agent_factory_;
  ::system_api::MockLibcFsApiOverride libc_fs_api_;
  ::system_api::MockLibcNetApiOverride libc_net_api_;
  ::system_api::MockLibcProcessApiOverride libc_process_api_;
  ::util::MockTimeUtilsOverride time_utils_;
};

TEST_F(IpcAgentTest, CreateDestroy) {
  // Implicitly tested by SetUp() and TearDown().
}

TEST_F(IpcAgentTest, WriteData) {
  const int kWriteSocket = 42;

  EXPECT_CALL(libc_net_api_.Mock(), Socket(AF_UNIX, SOCK_STREAM, 0))
      .WillOnce(Return(kWriteSocket));
  EXPECT_CALL(libc_net_api_.Mock(), Connect(kWriteSocket, HasUnixPath(), _))
      .WillOnce(Return(0));
  EXPECT_CALL(libc_net_api_.Mock(), Send(kWriteSocket, NotNull(), _, 0))
      .WillOnce(Return(0));

  EXPECT_OK(ipc_agent_->WriteData(kData));
}

void SetPid(int sockfd, int level, int optname, void *optval,
            socklen_t *optlen) {
  static_cast<struct ucred *>(optval)->pid = kPid;
}

void SetRecvData(int sockfd, void *buf, size_t len, int flags) {
  strncpy(static_cast<char *>(buf), kData, len);
}

TEST_F(IpcAgentTest, ReadData) {
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

// TODO(vmarmol): Add tests for failures in the different syscalls.
// TODO(vmarmol): Add integration tests for IpcAgent.

}  // namespace nscon
}  // namespace containers
