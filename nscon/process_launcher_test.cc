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

#include "nscon/process_launcher.h"

#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <memory>
#include <vector>

#include "nscon/configurator/ns_configurator_mock.h"
#include "nscon/ipc_agent_mock.h"
#include "nscon/ns_util_mock.h"
#include "include/namespaces.pb.h"
#include "util/errors_test_util.h"
#include "system_api/libc_process_api_test_util.h"
#include "gtest/gtest.h"
#include "util/process/mock_subprocess.h"

using ::std::pair;
using ::std::unique_ptr;
using ::std::vector;
using ::testing::Invoke;
using ::testing::NotNull;
using ::testing::Return;
using ::testing::SetArgumentPointee;
using ::testing::SetErrnoAndReturn;
using ::testing::StrEq;
using ::testing::_;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace nscon {

// Arguments to clone() that we wish to verify.
static struct CloneVerifierArgs {
  int clone_flags;
  bool remount_proc_sys_fs;
  vector<string> argv;

  // This is what will be returned from the CloneVerifier().
  pid_t return_val;
} g_clone_verifier_args;

static int CloneVerifier(int (*fn)(void *), void *stack, int flags,  // NOLINT
                         void *arg) {
  struct CloneArgs {
    char **argv;
    bool remount_proc_sys_fs;
    IpcAgent *ipc_agent;
    const vector<NsConfigurator *> *configurators;
    const NamespaceSpec *spec;
  };

  EXPECT_NE(nullptr, fn);
  EXPECT_NE(nullptr, stack);
  EXPECT_EQ(g_clone_verifier_args.clone_flags, flags);
  EXPECT_NE(nullptr, arg);
  CloneArgs *clone_args = static_cast<CloneArgs *>(arg);
  EXPECT_EQ(g_clone_verifier_args.remount_proc_sys_fs,
            clone_args->remount_proc_sys_fs);
  int i = 0;
  for (auto a : g_clone_verifier_args.argv) {
    EXPECT_NE(nullptr, clone_args->argv[i]);
    EXPECT_STREQ(a.c_str(), clone_args->argv[i]);
    i++;
  }
  EXPECT_EQ(nullptr, clone_args->argv[i]);

  return g_clone_verifier_args.return_val;
}

// Simply returns the specified subprocess.
SubProcess *IdentitySubProcessFactory(MockSubProcess *subprocess, IpcAgent *) {
  return subprocess;
}

class ProcessLauncherTest : public ::testing::Test {
 public:
  void SetUp() {
    mock_ns_util_.reset(new ::testing::StrictMock<MockNsUtil>());
    mock_subprocess_ = new ::testing::StrictMock<MockSubProcess>();
    mock_ipc_agent_.reset(new ::testing::StrictMock<MockIpcAgent>());
    mock_ipc_agent_factory_ = new ::testing::StrictMock<MockIpcAgentFactory>();
    SubProcessFactory *mock_subprocess_factory =
        NewPermanentCallback(&IdentitySubProcessFactory, mock_subprocess_);
    pl_.reset(new ProcessLauncher(mock_subprocess_factory,
                                  mock_ns_util_.get(),
                                  mock_ipc_agent_factory_));
  }

  // Wrappers for private methods
  StatusOr<bool> CallWaitForChildSuccess(pid_t pid) const {
    return pl_->WaitForChildSuccess(pid);
  }

  StatusOr<pid_t> CallForkAndLaunch(const vector<string> &argv) const {
    return pl_->ForkAndLaunch(argv);
  }

  StatusOr<pid_t> CallCloneAndLaunch(
      const vector<string> &argv,
      const vector<int> &namespaces,
      const vector<NsConfigurator *> configurators,
      const NamespaceSpec &spec) const {
    return pl_->CloneAndLaunch(argv, namespaces, configurators, spec);
  }

 protected:
  // Sets up expectations for Clone() call.
  void SetupCloneVerifier(const vector<int> &namespaces,
                          const vector<string> &argv, pid_t retval) {
    EXPECT_CALL(libc_process_api_.Mock(), Clone(_, _, _, _))
        .WillOnce(Invoke(&CloneVerifier));

    g_clone_verifier_args.clone_flags = SIGCHLD;
    for (auto ns : namespaces) {
      g_clone_verifier_args.clone_flags |= ns;
    }
    g_clone_verifier_args.argv.clear();
    g_clone_verifier_args.argv.insert(g_clone_verifier_args.argv.begin(),
                                      argv.begin(), argv.end());
    g_clone_verifier_args.return_val = retval;
  }

  unique_ptr<ProcessLauncher> pl_;
  unique_ptr<MockNsUtil> mock_ns_util_;
  MockSubProcess *mock_subprocess_;
  MockIpcAgentFactory *mock_ipc_agent_factory_;
  unique_ptr<MockIpcAgent> mock_ipc_agent_;
  system_api::MockLibcProcessApiOverride libc_process_api_;
  const pid_t kPid_ = 9999;
  const vector<string> kCommand_ = {"/bin/ls", "-l", "-h"};
};

TEST_F(ProcessLauncherTest, CloneAndLaunch_NoNamespaces) {
  NamespaceSpec spec;
  const vector<int> kNamespaces = {};

  EXPECT_CALL(*mock_ipc_agent_factory_, Create())
      .WillOnce(Return(mock_ipc_agent_.get()));
  EXPECT_CALL(*mock_ipc_agent_, Destroy())
      .WillOnce(Return(Status::OK));
  SetupCloneVerifier(kNamespaces, kCommand_, kPid_);
  EXPECT_CALL(*mock_ipc_agent_, WriteData(_))
      .WillOnce(Return(Status::OK));

  StatusOr<pid_t> statusor = CallCloneAndLaunch(kCommand_, kNamespaces, {},
                                                spec);
  ASSERT_OK(statusor);
  EXPECT_EQ(kPid_, statusor.ValueOrDie());
}

TEST_F(ProcessLauncherTest, CloneAndLaunch_NewNamespaces) {
  NamespaceSpec spec;
  const vector<int> kNamespaces = {CLONE_NEWIPC, CLONE_NEWNS};

  EXPECT_CALL(*mock_ipc_agent_factory_, Create())
      .WillOnce(Return(mock_ipc_agent_.get()));
  EXPECT_CALL(*mock_ipc_agent_, Destroy())
      .WillOnce(Return(Status::OK));
  SetupCloneVerifier(kNamespaces, kCommand_, kPid_);
  EXPECT_CALL(*mock_ipc_agent_, WriteData(_))
      .WillOnce(Return(Status::OK));

  StatusOr<pid_t> statusor = CallCloneAndLaunch(kCommand_, kNamespaces, {},
                                                spec);
  ASSERT_OK(statusor);
  EXPECT_EQ(kPid_, statusor.ValueOrDie());
}

TEST_F(ProcessLauncherTest, CloneAndLaunch_InvalidCommand) {
  // TODO(adityakali): Simulate read() on pipe returns error message.
}

TEST_F(ProcessLauncherTest, CloneAndLaunch_CloneFailure) {
  NamespaceSpec spec;
  // Invalid namespace flags.
  const vector<int> kNamespaces = {CLONE_FS, CLONE_VM};

  EXPECT_CALL(*mock_ipc_agent_factory_, Create())
      .WillOnce(Return(mock_ipc_agent_.get()));
  EXPECT_CALL(*mock_ipc_agent_, Destroy())
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(libc_process_api_.Mock(), Clone(_, _, _, _))
      .WillOnce(SetErrnoAndReturn(ENOMEM, -1));

  EXPECT_ERROR_CODE(::util::error::INTERNAL,
                    CallCloneAndLaunch(kCommand_, kNamespaces, {}, spec));
}

TEST_F(ProcessLauncherTest, Launch_AttachFailure) {
  // Invalid namespace flags.
  const vector<int> kNamespaces = {CLONE_FS, CLONE_VM};
  pid_t ns_target = kPid_;

  EXPECT_CALL(*mock_ns_util_, AttachNamespaces(kNamespaces, ns_target))
      .WillOnce(Return(Status(::util::error::INVALID_ARGUMENT, "Invalid Arg")));
  EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT,
                    pl_->Launch(kCommand_, kNamespaces, ns_target));
}

TEST_F(ProcessLauncherTest, WaitForChildSuccess_NormalExit) {
  const pid_t kPid = 9999;
  EXPECT_CALL(libc_process_api_.Mock(), WaitPid(kPid, NotNull(), _))
      .WillOnce(DoAll(SetArgumentPointee<1>(W_EXITCODE(0, 0)), Return(kPid)));
  StatusOr<bool> statusor = CallWaitForChildSuccess(kPid);
  ASSERT_OK(statusor);
  ASSERT_TRUE(statusor.ValueOrDie());
}

TEST_F(ProcessLauncherTest, WaitForChildSuccess_AbnormalExit) {
  const pid_t kPid = 9999;
  EXPECT_CALL(libc_process_api_.Mock(), WaitPid(kPid, NotNull(), _))
      .WillOnce(DoAll(SetArgumentPointee<1>(W_EXITCODE(-1, 0)), Return(kPid)));
  StatusOr<bool> statusor = CallWaitForChildSuccess(kPid);
  ASSERT_OK(statusor);
  ASSERT_FALSE(statusor.ValueOrDie());
}

TEST_F(ProcessLauncherTest, WaitForChildSuccess_SignaledExit) {
  const pid_t kPid = 9999;
  EXPECT_CALL(libc_process_api_.Mock(), WaitPid(kPid, NotNull(), _))
      .WillOnce(DoAll(SetArgumentPointee<1>(W_EXITCODE(0, SIGKILL)),
                     Return(kPid)));
  StatusOr<bool> statusor = CallWaitForChildSuccess(kPid);
  ASSERT_OK(statusor);
  ASSERT_FALSE(statusor.ValueOrDie());
}

TEST_F(ProcessLauncherTest, WaitForChildSuccess_InvalidChild) {
  const pid_t kPid = 9999;
  EXPECT_CALL(libc_process_api_.Mock(), WaitPid(kPid, NotNull(), _))
      .WillOnce(SetErrnoAndReturn(-ECHILD, -1));
  EXPECT_ERROR_CODE(::util::error::INTERNAL, CallWaitForChildSuccess(kPid));
}

TEST_F(ProcessLauncherTest, ForkAndLaunch) {
  const vector<int> kNamespaces = {CLONE_NEWPID, CLONE_NEWIPC};
  const pid_t kNewPid = 8888;
  const pid_t kNewNewPid = 7777;

  EXPECT_CALL(*mock_ipc_agent_factory_, Create())
      .WillOnce(Return(mock_ipc_agent_.get()));
  EXPECT_CALL(libc_process_api_.Mock(), Fork()).WillOnce(Return(kNewPid));
  EXPECT_CALL(libc_process_api_.Mock(), WaitPid(kNewPid, NotNull(), _))
      .WillOnce(DoAll(SetArgumentPointee<1>(W_EXITCODE(0, 0)),
                      Return(kNewPid)));
  EXPECT_CALL(*mock_ipc_agent_, ReadData())
      .WillOnce(Return(pair<string, pid_t>("", kNewNewPid)));
  EXPECT_CALL(*mock_ipc_agent_, Destroy())
      .WillOnce(Return(Status::OK));

  StatusOr<pid_t> newstatusor = CallForkAndLaunch(kCommand_);
  ASSERT_OK(newstatusor);
  EXPECT_EQ(kNewNewPid, newstatusor.ValueOrDie());
}

TEST_F(ProcessLauncherTest, ForkAndLaunch_ChildAbnormalExit) {
  const vector<int> kNamespaces = {CLONE_NEWPID, CLONE_NEWIPC};
  const pid_t kNewPid = 8888;

  EXPECT_CALL(*mock_ipc_agent_factory_, Create())
      .WillOnce(Return(mock_ipc_agent_.get()));
  EXPECT_CALL(libc_process_api_.Mock(), Fork()).WillOnce(Return(kNewPid));
  EXPECT_CALL(libc_process_api_.Mock(), WaitPid(kNewPid, NotNull(), _))
      .WillOnce(DoAll(SetArgumentPointee<1>(W_EXITCODE(-1, 0)),
                      Return(kNewPid)));
  EXPECT_CALL(*mock_ipc_agent_, ReadData())
      .WillOnce(Return(pair<string, pid_t>("Abnormal Child exit", kNewPid)));
  EXPECT_CALL(*mock_ipc_agent_, Destroy())
      .WillOnce(Return(Status::OK));

  EXPECT_ERROR_CODE(::util::error::INTERNAL, CallForkAndLaunch(kCommand_));
}

// TODO(adityakali): Add tests for ForkAndLaunch()
// TODO(adityakali): Add tests for CloneAndLaunch() with configurators.
// TODO(adityakali): Add tests for ProcessLauncher::CloneFn()

}  // namespace nscon
}  // namespace containers
