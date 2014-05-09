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
#include "system_api/libc_fs_api_test_util.h"
#include "system_api/libc_process_api_test_util.h"
#include "gtest/gtest.h"

using ::std::pair;
using ::std::unique_ptr;
using ::std::vector;
using ::testing::ContainerEq;
#include "util/testing/equals_initialized_proto.h"
using ::testing::EqualsInitializedProto;
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

// To mock ProcessLauncher::run_spec_configurator_.
class MockRunSpecConfigurator : public RunSpecConfigurator {
 public:
  MockRunSpecConfigurator() : RunSpecConfigurator(nullptr) {}

  MOCK_CONST_METHOD2(Configure,
                     ::util::Status(const RunSpec &run_spec,
                                    const vector<int> &fd_whitelist));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockRunSpecConfigurator);
};

// Arguments to clone() that we wish to verify.
static struct CloneVerifierArgs {
  int clone_flags;
  vector<string> argv;
  int console_fd;

  // This is what will be returned from the CloneVerifier().
  pid_t return_val;
} g_clone_verifier_args;

static int CloneVerifier(int (*fn)(void *), void *stack, int flags,  // NOLINT
                         void *arg) {
  struct CloneArgs {
    char **argv;
    int clone_flags;
    int console_fd;
    IpcAgent *ipc_agent;
    NsUtil *ns_util;
    const RunSpecConfigurator *runconfig;
    const RunSpec *run_spec;
    const vector<NsConfigurator *> *configurators;
    const NamespaceSpec *spec;
    IpcAgent *pid_notification_agent;
  };

  EXPECT_NE(nullptr, fn);
  EXPECT_NE(nullptr, stack);
  EXPECT_EQ(g_clone_verifier_args.clone_flags, flags);
  EXPECT_NE(nullptr, arg);
  CloneArgs *clone_args = static_cast<CloneArgs *>(arg);
  EXPECT_EQ(g_clone_verifier_args.clone_flags, clone_args->clone_flags);
  EXPECT_EQ(g_clone_verifier_args.console_fd, clone_args->console_fd);
  int i = 0;
  for (auto a : g_clone_verifier_args.argv) {
    EXPECT_NE(nullptr, clone_args->argv[i]);
    EXPECT_STREQ(a.c_str(), clone_args->argv[i]);
    i++;
  }
  EXPECT_EQ(nullptr, clone_args->argv[i]);

  return g_clone_verifier_args.return_val;
}

class ProcessLauncherTest : public ::testing::Test {
 public:
  void SetUp() override {
    mock_ns_util_.reset(new ::testing::StrictMock<MockNsUtil>());
    mock_ipc_agent_.reset(new ::testing::StrictMock<MockIpcAgent>());
    mock_ipc_agent_factory_ = new ::testing::StrictMock<MockIpcAgentFactory>();
    mock_runconfig_ = new ::testing::StrictMock<MockRunSpecConfigurator>();
    pl_.reset(new ProcessLauncher(mock_ns_util_.get(),
                                  mock_ipc_agent_factory_,
                                  mock_runconfig_));
  }

  // Wrappers for private methods
  StatusOr<int> CallGetConsoleFd(const RunSpec_Console &console) {
    return pl_->GetConsoleFd(console);
  }

 protected:
  // Sets up expectations for Clone() call.
  void SetupCloneVerifier(const vector<int> &namespaces,
                          const vector<string> &argv, pid_t retval,
                          int console_fd) {
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
    g_clone_verifier_args.console_fd = console_fd;
  }

  unique_ptr<ProcessLauncher> pl_;
  unique_ptr<MockNsUtil> mock_ns_util_;
  MockRunSpecConfigurator *mock_runconfig_;
  MockIpcAgentFactory *mock_ipc_agent_factory_;
  unique_ptr<MockIpcAgent> mock_ipc_agent_;
  system_api::MockLibcFsApiOverride libc_fs_api_;
  system_api::MockLibcProcessApiOverride libc_process_api_;
  const pid_t kPid_ = 9999;
  const vector<string> kCommand_ = {"/bin/ls", "-l", "-h"};
  char *command_array_[4] = { const_cast<char *>("/bin/ls"),
                              const_cast<char *>("-l"),
                              const_cast<char *>("-h"), nullptr };
  const int kConsoleFd_ = 10;
};

typedef ProcessLauncherTest NewNsProcessTest;

TEST_F(NewNsProcessTest, NoNamespaces) {
  NamespaceSpec spec;
  RunSpec run_spec;
  const vector<int> kNamespaces = {};

  EXPECT_CALL(*mock_ipc_agent_factory_, Create())
      .WillOnce(Return(mock_ipc_agent_.get()));
  EXPECT_CALL(*mock_ipc_agent_, Destroy())
      .WillOnce(Return(Status::OK));
  SetupCloneVerifier(kNamespaces, kCommand_, kPid_, -1);
  EXPECT_CALL(*mock_ipc_agent_, WriteData(_))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_ipc_agent_, WaitForChild())
      .WillOnce(Return(Status::CANCELLED));

  StatusOr<pid_t> statusor =
      pl_->NewNsProcess(kCommand_, kNamespaces, {}, spec, run_spec);
  ASSERT_OK(statusor);
  EXPECT_EQ(kPid_, statusor.ValueOrDie());
}

TEST_F(NewNsProcessTest, NewNamespaces) {
  NamespaceSpec spec;
  RunSpec run_spec;
  const vector<int> kNamespaces = {CLONE_NEWPID, CLONE_NEWIPC, CLONE_NEWNS};

  EXPECT_CALL(*mock_ipc_agent_factory_, Create())
      .WillOnce(Return(mock_ipc_agent_.get()));
  EXPECT_CALL(*mock_ipc_agent_, Destroy())
      .WillOnce(Return(Status::OK));
  SetupCloneVerifier(kNamespaces, kCommand_, kPid_, -1);
  EXPECT_CALL(*mock_ipc_agent_, WriteData(_))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_ipc_agent_, WaitForChild())
      .WillOnce(Return(Status::CANCELLED));

  StatusOr<pid_t> statusor =
      pl_->NewNsProcess(kCommand_, kNamespaces, {}, spec, run_spec);
  ASSERT_OK(statusor);
  EXPECT_EQ(kPid_, statusor.ValueOrDie());
}

TEST_F(NewNsProcessTest, SuccessWithConsole) {
  const char kSlavePty[] = "10";
  NamespaceSpec spec;
  RunSpec run_spec;
  run_spec.mutable_console()->set_slave_pty(kSlavePty);
  const vector<int> kNamespaces = {CLONE_NEWPID, CLONE_NEWIPC, CLONE_NEWNS};

  EXPECT_CALL(*mock_ns_util_, OpenSlavePtyDevice(StrEq(kSlavePty)))
      .WillOnce(Return(kConsoleFd_));
  EXPECT_CALL(libc_fs_api_.Mock(), Close(kConsoleFd_)).WillOnce(Return(0));
  EXPECT_CALL(*mock_ipc_agent_factory_, Create())
      .WillOnce(Return(mock_ipc_agent_.get()));
  EXPECT_CALL(*mock_ipc_agent_, Destroy())
      .WillOnce(Return(Status::OK));
  SetupCloneVerifier(kNamespaces, kCommand_, kPid_, kConsoleFd_);
  EXPECT_CALL(*mock_ipc_agent_, WriteData(_))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_ipc_agent_, WaitForChild())
      .WillOnce(Return(Status::CANCELLED));

  StatusOr<pid_t> statusor =
      pl_->NewNsProcess(kCommand_, kNamespaces, {}, spec, run_spec);
  ASSERT_OK(statusor);
  EXPECT_EQ(kPid_, statusor.ValueOrDie());
}

TEST_F(NewNsProcessTest, InvalidCommand) {
  // In this case, exec() failed and the child will return error via ipc_agent.
  NamespaceSpec spec;
  RunSpec run_spec;
  const vector<int> kNamespaces = {CLONE_NEWIPC, CLONE_NEWNS};

  EXPECT_CALL(*mock_ipc_agent_factory_, Create())
      .WillOnce(Return(mock_ipc_agent_.get()));
  EXPECT_CALL(*mock_ipc_agent_, Destroy())
      .WillOnce(Return(Status::OK));
  SetupCloneVerifier(kNamespaces, kCommand_, kPid_, -1);
  EXPECT_CALL(*mock_ipc_agent_, WriteData(_))
      .WillOnce(Return(Status::OK));

  // Status:OK implies that child sent us error message.
  EXPECT_CALL(*mock_ipc_agent_, WaitForChild()).WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_ipc_agent_, ReadData())
      .WillOnce(Return(pair<string, pid_t>("execve() failed", 0)));

  EXPECT_ERROR_CODE(::util::error::INTERNAL,
                    pl_->NewNsProcess(kCommand_, kNamespaces, {}, spec,
                                      run_spec));
}

TEST_F(NewNsProcessTest, CloneFailure) {
  NamespaceSpec spec;
  RunSpec run_spec;
  // Invalid namespace flags.
  const vector<int> kNamespaces = {CLONE_FS, CLONE_VM};

  EXPECT_CALL(*mock_ipc_agent_factory_, Create())
      .WillOnce(Return(mock_ipc_agent_.get()));
  EXPECT_CALL(*mock_ipc_agent_, Destroy())
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(libc_process_api_.Mock(), Clone(_, _, _, _))
      .WillOnce(SetErrnoAndReturn(ENOMEM, -1));

  EXPECT_ERROR_CODE(::util::error::INTERNAL,
                    pl_->NewNsProcess(kCommand_, kNamespaces, {}, spec,
                                      run_spec));
}

typedef ProcessLauncherTest GetConsoleFdTest;

TEST_F(GetConsoleFdTest, Success) {
  const char kSlavePty[] = "10";
  RunSpec_Console console;
  console.set_slave_pty(kSlavePty);
  EXPECT_CALL(*mock_ns_util_, OpenSlavePtyDevice(StrEq(kSlavePty)))
      .WillOnce(Return(kConsoleFd_));
  StatusOr<int> statusor = CallGetConsoleFd(console);
  ASSERT_OK(statusor);
  EXPECT_EQ(kConsoleFd_, statusor.ValueOrDie());
}

TEST_F(GetConsoleFdTest, OpenSlavePtyDeviceError) {
  const char kSlavePty[] = "10";
  RunSpec_Console console;
  console.set_slave_pty(kSlavePty);
  EXPECT_CALL(*mock_ns_util_, OpenSlavePtyDevice(StrEq(kSlavePty)))
      .WillOnce(Return(Status(::util::error::NOT_FOUND, "blah")));
  EXPECT_ERROR_CODE(::util::error::NOT_FOUND, CallGetConsoleFd(console));
}

TEST_F(GetConsoleFdTest, ConsoleEmptySlavePty) {
  RunSpec_Console console;
  console.set_slave_pty("");
  EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT, CallGetConsoleFd(console));
}

TEST_F(GetConsoleFdTest, ConsoleEmpty) {
  RunSpec_Console console;
  EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT, CallGetConsoleFd(console));
}

typedef ProcessLauncherTest NewNsProcessInTargetTest;

TEST_F(NewNsProcessInTargetTest, Success) {
  const vector<int> kNamespaces = {CLONE_NEWPID, CLONE_NEWIPC};
  pid_t ns_target = kPid_;
  const pid_t kNewPid = 8888;
  const pid_t kNewNewPid = 7777;
  RunSpec run_spec;
  // NewNsProcessInTarget creates 2 IpcAgent objects.
  unique_ptr<MockIpcAgent> mock_err_agent(
      new ::testing::StrictMock<MockIpcAgent>());
  unique_ptr<MockIpcAgent> mock_pid_notification_agent(
      new ::testing::StrictMock<MockIpcAgent>());

  EXPECT_CALL(*mock_ns_util_, AttachNamespaces(kNamespaces, ns_target))
      .WillOnce(Return(Status::OK));

  EXPECT_CALL(*mock_ipc_agent_factory_, Create())
      .WillOnce(Return(mock_pid_notification_agent.get()))
      .RetiresOnSaturation();
  EXPECT_CALL(*mock_ipc_agent_factory_, Create())
      .WillOnce(Return(mock_err_agent.get())).RetiresOnSaturation();

  EXPECT_CALL(libc_process_api_.Mock(), Fork()).WillOnce(Return(kNewPid));

  EXPECT_CALL(*mock_err_agent, WaitForChild())
      .WillOnce(Return(Status::CANCELLED));
  EXPECT_CALL(*mock_pid_notification_agent, ReadData())
      .WillOnce(Return(pair<string, pid_t>("pid", kNewNewPid)));

  EXPECT_CALL(*mock_err_agent, Destroy()).WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_pid_notification_agent, Destroy())
      .WillOnce(Return(Status::OK));

  StatusOr<pid_t> newstatusor =
      pl_->NewNsProcessInTarget(kCommand_, kNamespaces, ns_target, run_spec);
  ASSERT_OK(newstatusor);
  EXPECT_EQ(kNewNewPid, newstatusor.ValueOrDie());
}

TEST_F(NewNsProcessInTargetTest, AttachFailure) {
  // Invalid namespace flags.
  const vector<int> kNamespaces = {CLONE_FS, CLONE_VM};
  pid_t ns_target = kPid_;
  RunSpec run_spec;

  EXPECT_CALL(*mock_ns_util_, AttachNamespaces(kNamespaces, ns_target))
      .WillOnce(Return(Status(::util::error::INVALID_ARGUMENT, "Invalid Arg")));
  EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT,
                    pl_->NewNsProcessInTarget(kCommand_, kNamespaces, ns_target,
                                              run_spec));
}

TEST_F(NewNsProcessInTargetTest, ChildEncountersError) {
  const vector<int> kNamespaces = {CLONE_NEWPID, CLONE_NEWIPC};
  const pid_t kNewPid = 8888;
  pid_t ns_target = kPid_;
  RunSpec run_spec;
  // NewNsProcessInTarget creates 2 IpcAgent objects.
  unique_ptr<MockIpcAgent> mock_err_agent(
      new ::testing::StrictMock<MockIpcAgent>());
  unique_ptr<MockIpcAgent> mock_pid_notification_agent(
      new ::testing::StrictMock<MockIpcAgent>());

  EXPECT_CALL(*mock_ns_util_, AttachNamespaces(kNamespaces, ns_target))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_ipc_agent_factory_, Create())
      .WillOnce(Return(mock_pid_notification_agent.get()))
      .RetiresOnSaturation();
  EXPECT_CALL(*mock_ipc_agent_factory_, Create())
      .WillOnce(Return(mock_err_agent.get())).RetiresOnSaturation();

  EXPECT_CALL(libc_process_api_.Mock(), Fork()).WillOnce(Return(kNewPid));

  // Status:OK implies that child sent us error message.
  EXPECT_CALL(*mock_err_agent, WaitForChild())
      .WillOnce(Return(Status::OK));

  EXPECT_CALL(*mock_err_agent, ReadData())
      .WillOnce(Return(pair<string, pid_t>("Child error", kNewPid)));

  EXPECT_CALL(*mock_err_agent, Destroy()).WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_pid_notification_agent, Destroy())
      .WillOnce(Return(Status::OK));

  EXPECT_ERROR_CODE(::util::error::INTERNAL,
                    pl_->NewNsProcessInTarget(kCommand_, kNamespaces, ns_target,
                                              run_spec));
}

typedef ProcessLauncherTest CloneFnTest;

TEST_F(CloneFnTest, Success) {
  NamespaceSpec spec;
  RunSpec run_spec;
  int clone_flags = SIGCHLD | CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWIPC;
  vector<int> fd_whitelist = { 0, 1, 2 };

  EXPECT_CALL(*mock_ipc_agent_, ReadData())
      .WillOnce(Return(pair<string, pid_t>("RESUME", 0)));
  EXPECT_CALL(libc_process_api_.Mock(), SetSid())
      .WillOnce(Return(0));
  EXPECT_CALL(*mock_ns_util_, AttachToConsoleFd(kConsoleFd_))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_runconfig_,
              Configure(EqualsInitializedProto(run_spec),
                        ContainerEq(fd_whitelist)))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(libc_process_api_.Mock(),
              Execve(StrEq(command_array_[0]), command_array_, NotNull()))
      .WillOnce(Return(0));

  // Even if this is success, execve() returning implies error. So we rely on
  // the expectations set above to capture failure.
  EXPECT_ERROR_CODE(::util::error::INTERNAL,
                    ProcessLauncher::CloneFn(command_array_, clone_flags,
                                             kConsoleFd_,
                                             mock_ipc_agent_.get(),
                                             mock_ns_util_.get(),
                                             mock_runconfig_,
                                             &run_spec, {}, &spec, nullptr));
}

TEST_F(CloneFnTest, AttachToConsoleFailure) {
  NamespaceSpec spec;
  RunSpec run_spec;
  run_spec.mutable_console()->set_slave_pty("10");
  int clone_flags = SIGCHLD | CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWIPC;

  EXPECT_CALL(*mock_ipc_agent_, ReadData())
      .WillOnce(Return(pair<string, pid_t>("RESUME", 0)));
  EXPECT_CALL(libc_process_api_.Mock(), SetSid())
      .WillOnce(Return(0));
  EXPECT_CALL(*mock_ns_util_, AttachToConsoleFd(kConsoleFd_))
      .WillOnce(Return(Status(::util::error::NOT_FOUND, "blah")));

  EXPECT_ERROR_CODE(::util::error::NOT_FOUND,
                    ProcessLauncher::CloneFn(command_array_,
                                             clone_flags,
                                             kConsoleFd_,
                                             mock_ipc_agent_.get(),
                                             mock_ns_util_.get(),
                                             mock_runconfig_,
                                             &run_spec,
                                             {},  // configurators
                                             &spec,
                                             nullptr));
}

// TODO(adityakali): Add tests for NewNsProcessInTarget()
// TODO(adityakali): Add tests for NewNsProcess() with configurators.
// TODO(adityakali): Add tests for ProcessLauncher::CloneFn()

// Tests for RunSpecConfigurator class
class RunSpecConfiguratorTest : public ::testing::Test {
 public:
  void SetUp() override {
    mock_ns_util_.reset(new ::testing::StrictMock<MockNsUtil>());
    runconfig_.reset(new RunSpecConfigurator(mock_ns_util_.get()));
  }

 protected:
  unique_ptr<MockNsUtil> mock_ns_util_;
  unique_ptr<RunSpecConfigurator> runconfig_;
  system_api::MockLibcFsApiOverride libc_fs_api_;
  system_api::MockLibcProcessApiOverride libc_process_api_;
};

typedef RunSpecConfiguratorTest ConfigureTest;

TEST_F(ConfigureTest, EmptyRunSpec) {
  RunSpec run_spec;
  vector<int> open_fds = {0, 1, 2, 3, 99, 1001 };

  EXPECT_CALL(libc_process_api_.Mock(), SetGroups(0, _)).WillOnce(Return(0));
  EXPECT_CALL(*mock_ns_util_, GetOpenFDs()).WillOnce(Return(open_fds));
  for (auto fd : open_fds) {
    EXPECT_CALL(libc_fs_api_.Mock(), FCntl(fd, F_SETFD, FD_CLOEXEC))
        .WillOnce(Return(0));
  }
  EXPECT_OK(runconfig_->Configure(run_spec, {}));
}

TEST_F(ConfigureTest, Success) {
  uid_t kUid = 1000;
  gid_t kGid = 2000;
  RunSpec run_spec;
  run_spec.set_uid(kUid);
  run_spec.set_gid(kGid);
  run_spec.add_groups(kGid);
  run_spec.add_groups(kGid+1);
  run_spec.add_groups(kGid+2);
  vector<int> open_fds = {0, 1, 2, 3, 99, 1001 };

  EXPECT_CALL(libc_process_api_.Mock(), SetGroups(3, _)).WillOnce(Return(0));
  EXPECT_CALL(libc_process_api_.Mock(), SetResGid(kGid, kGid, kGid))
      .WillOnce(Return(0));
  EXPECT_CALL(libc_process_api_.Mock(), SetResUid(kUid, kUid, kUid))
      .WillOnce(Return(0));
  EXPECT_CALL(*mock_ns_util_, GetOpenFDs()).WillOnce(Return(open_fds));
  for (auto fd : open_fds) {
    EXPECT_CALL(libc_fs_api_.Mock(), FCntl(fd, F_SETFD, FD_CLOEXEC))
        .WillOnce(Return(0));
  }
  EXPECT_OK(runconfig_->Configure(run_spec, {}));
}

TEST_F(ConfigureTest, SuccessWithFdWhitelist) {
  uid_t kUid = 1000;
  gid_t kGid = 2000;
  RunSpec run_spec;
  run_spec.set_uid(kUid);
  run_spec.set_gid(kGid);
  run_spec.add_groups(kGid);
  run_spec.add_groups(kGid+1);
  run_spec.add_groups(kGid+2);
  vector<int> open_fds = {0, 1, 2, 3, 99, 1001 };
  vector<int> fd_whitelist = {99, 1001};

  EXPECT_CALL(libc_process_api_.Mock(), SetGroups(3, _)).WillOnce(Return(0));
  EXPECT_CALL(libc_process_api_.Mock(), SetResGid(kGid, kGid, kGid))
      .WillOnce(Return(0));
  EXPECT_CALL(libc_process_api_.Mock(), SetResUid(kUid, kUid, kUid))
      .WillOnce(Return(0));
  EXPECT_CALL(*mock_ns_util_, GetOpenFDs()).WillOnce(Return(open_fds));
  for (auto fd : {0, 1, 2, 3}) {
    EXPECT_CALL(libc_fs_api_.Mock(), FCntl(fd, F_SETFD, FD_CLOEXEC))
        .WillOnce(Return(0));
  }
  EXPECT_OK(runconfig_->Configure(run_spec, fd_whitelist));
}

TEST_F(ConfigureTest, SetResGidFailure) {
  uid_t kUid = 1000;
  gid_t kGid = 2000;
  RunSpec run_spec;
  run_spec.set_uid(kUid);
  run_spec.set_gid(kGid);

  EXPECT_CALL(libc_process_api_.Mock(), SetGroups(0, _)).WillOnce(Return(0));
  EXPECT_CALL(libc_process_api_.Mock(), SetResGid(kGid, kGid, kGid))
      .WillOnce(Return(-1));
  EXPECT_ERROR_CODE(::util::error::INTERNAL,
                    runconfig_->Configure(run_spec, {}));
}

TEST_F(ConfigureTest, SetResUidFailure) {
  uid_t kUid = 1000;
  gid_t kGid = 2000;
  RunSpec run_spec;
  run_spec.set_uid(kUid);
  run_spec.set_gid(kGid);

  EXPECT_CALL(libc_process_api_.Mock(), SetGroups(0, _)).WillOnce(Return(0));
  EXPECT_CALL(libc_process_api_.Mock(), SetResGid(kGid, kGid, kGid))
      .WillOnce(Return(0));
  EXPECT_CALL(libc_process_api_.Mock(), SetResUid(kUid, kUid, kUid))
      .WillOnce(Return(-1));
  EXPECT_ERROR_CODE(::util::error::INTERNAL,
                    runconfig_->Configure(run_spec, {}));
}

TEST_F(ConfigureTest, GetOpenFDsFailure) {
  uid_t kUid = 1000;
  gid_t kGid = 2000;
  RunSpec run_spec;
  run_spec.set_uid(kUid);
  run_spec.set_gid(kGid);
  run_spec.add_groups(kGid);
  run_spec.add_groups(kGid+1);
  run_spec.add_groups(kGid+2);

  EXPECT_CALL(libc_process_api_.Mock(), SetGroups(3, _)).WillOnce(Return(0));
  EXPECT_CALL(libc_process_api_.Mock(), SetResGid(kGid, kGid, kGid))
      .WillOnce(Return(0));
  EXPECT_CALL(libc_process_api_.Mock(), SetResUid(kUid, kUid, kUid))
      .WillOnce(Return(0));
  EXPECT_CALL(*mock_ns_util_, GetOpenFDs())
      .WillOnce(Return(Status(::util::error::INTERNAL, "error")));
  EXPECT_ERROR_CODE(::util::error::INTERNAL,
                    runconfig_->Configure(run_spec, {}));
}

TEST_F(ConfigureTest, FCntlFailure) {
  uid_t kUid = 1000;
  gid_t kGid = 2000;
  RunSpec run_spec;
  run_spec.set_uid(kUid);
  run_spec.set_gid(kGid);
  run_spec.add_groups(kGid);
  run_spec.add_groups(kGid+1);
  run_spec.add_groups(kGid+2);
  vector<int> open_fds = {0, 1, 2, 3, 99, 1001 };

  EXPECT_CALL(libc_process_api_.Mock(), SetGroups(3, _)).WillOnce(Return(0));
  EXPECT_CALL(libc_process_api_.Mock(), SetResGid(kGid, kGid, kGid))
      .WillOnce(Return(0));
  EXPECT_CALL(libc_process_api_.Mock(), SetResUid(kUid, kUid, kUid))
      .WillOnce(Return(0));
  EXPECT_CALL(*mock_ns_util_, GetOpenFDs()).WillOnce(Return(open_fds));
  EXPECT_CALL(libc_fs_api_.Mock(), FCntl(_, F_SETFD, FD_CLOEXEC))
      .WillRepeatedly(SetErrnoAndReturn(EBADF, -1));
  // fcntl() failure is ignored. So we should still succeed in this case.
  EXPECT_OK(runconfig_->Configure(run_spec, {}));
}

}  // namespace nscon
}  // namespace containers
