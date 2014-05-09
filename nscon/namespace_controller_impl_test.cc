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
// NamespaceController Implementation.
//

#include "nscon/namespace_controller_impl.h"

#include <sched.h>

#include "gflags/gflags.h"
#include "google/protobuf/text_format.h"
#include "nscon/ns_handle_mock.h"
#include "nscon/ns_util_mock.h"
#include "util/errors_test_util.h"
#include "system_api/libc_fs_api_test_util.h"
#include "system_api/libc_process_api_test_util.h"
#include "strings/join.h"
#include "strings/substitute.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "util/process/mock_subprocess.h"

DECLARE_string(nscon_path);
DECLARE_string(nsinit_path);
DECLARE_uint64(nsinit_uid);
DECLARE_uint64(nsinit_gid);

using ::std::unique_ptr;
using ::strings::Substitute;
using ::testing::ContainerEq;
using ::testing::Eq;
using ::testing::NotNull;
using ::testing::Return;
using ::testing::SetArrayArgument;
using ::testing::SetArgPointee;
using ::testing::StrEq;
using ::testing::StrictMock;
using ::testing::_;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace nscon {

const pid_t kPid = 9999;
const char *kHandleStr = "c3735928559-9999";

// Simply returns the specified subprocess.
SubProcess *IdentitySubProcessFactory(MockSubProcess *subprocess) {
  return subprocess;
}

static const string SpecToStr(const google::protobuf::Message &spec) {
  string spec_str;
  google::protobuf::TextFormat::Printer printer;
  printer.SetSingleLineMode(true);
  CHECK(printer.PrintToString(spec, &spec_str));
  return spec_str;
}

///////////////////////////////////////////////////////////////////////////////
// NamespaceControllerFactoryImpl class tests

class NamespaceControllerFactoryImplTest : public ::testing::Test {
 public:
  NamespaceControllerFactoryImplTest() : pipefd_{10, 11} {}
  ~NamespaceControllerFactoryImplTest() {}

  void SetUp() {
    mock_nshandle_ = new StrictMock<MockNsHandle>();
    mock_nsh_factory_ = new StrictMock<MockNsHandleFactory>();
    mock_subprocess_ = new StrictMock<MockSubProcess>();
    mock_ns_util_ = new StrictMock<MockNsUtil>();
    SubProcessFactory *mock_subprocess_factory_ =
        NewPermanentCallback(&IdentitySubProcessFactory, mock_subprocess_);
    nscon_factory_.reset(
        new NamespaceControllerFactoryImpl(mock_nsh_factory_,
                                           mock_subprocess_factory_,
                                           mock_ns_util_));
  }

  string GetOutputFdArg() {
    return Substitute("--nscon_output_fd=$0", pipefd_[1]);
  }

  void ExpectDefaultSubprocessCalls(const vector<string> &argv,
                                    int error_code) {
    EXPECT_CALL(*mock_subprocess_, SetChannelAction(_, _)).Times(3);
    EXPECT_CALL(*mock_subprocess_, SetArgv(ContainerEq(argv)));
    EXPECT_CALL(*mock_subprocess_, Start()).WillOnce(Return(true));
    EXPECT_CALL(*mock_subprocess_, Wait()).WillOnce(Return(true));
    EXPECT_CALL(*mock_subprocess_, exit_code())
        .WillRepeatedly(Return(error_code));
  }

  void ExpectPipeOpen() {
    EXPECT_CALL(mock_libc_fs_api_.Mock(), Pipe(_))
        .WillOnce(DoAll(SetArrayArgument<0>(pipefd_, &pipefd_[2]), Return(0)));
    EXPECT_CALL(mock_libc_fs_api_.Mock(), Close(Eq(pipefd_[0])));
    EXPECT_CALL(mock_libc_fs_api_.Mock(), Close(Eq(pipefd_[1])));
  }

  void ExpectPipeOutput(const char *data) {
    char *tdata = const_cast<char *>(data);
    EXPECT_CALL(mock_libc_fs_api_.Mock(), Read(Eq(pipefd_[0]), _, _))
        .WillOnce(DoAll(SetArrayArgument<1>(tdata, tdata + strlen(tdata) + 1),
                        Return(strlen(tdata))));
  }

  void ExpectNsHandlerFactoryGetCall(const string &handle) {
    EXPECT_CALL(*mock_nsh_factory_, Get(handle))
        .WillRepeatedly(Return(mock_nshandle_));
  }

 protected:
  unique_ptr<NamespaceControllerFactoryImpl> nscon_factory_;
  ::system_api::MockLibcFsApiOverride mock_libc_fs_api_;
  MockNsHandle *mock_nshandle_;
  MockNsHandleFactory *mock_nsh_factory_;
  MockSubProcess *mock_subprocess_;
  MockNsUtil *mock_ns_util_;
  gflags::FlagSaver flag_saver_;
  int pipefd_[2];
};

TEST_F(NamespaceControllerFactoryImplTest, GetWithPid) {
  EXPECT_CALL(*mock_nsh_factory_, Get(kPid)).WillOnce(Return(mock_nshandle_));
  StatusOr<NamespaceController *> statusor = nscon_factory_->Get(kPid);
  EXPECT_OK(statusor);
  delete statusor.ValueOrDie();
}

TEST_F(NamespaceControllerFactoryImplTest, GetWithHandlestr) {
  EXPECT_CALL(*mock_nsh_factory_, Get(kHandleStr))
      .WillOnce(Return(mock_nshandle_));
  StatusOr<NamespaceController *> statusor = nscon_factory_->Get(kHandleStr);
  EXPECT_OK(statusor);
  delete statusor.ValueOrDie();
}

TEST_F(NamespaceControllerFactoryImplTest, Create) {
  NamespaceSpec spec;
  spec.mutable_pid();
  spec.mutable_mnt();
  spec.mutable_ipc();

  // Compose expected command with all default flags.
  const string kInitUid = Substitute("--nsinit_uid=$0", FLAGS_nsinit_uid);
  const string kInitGid = Substitute("--nsinit_gid=$0", FLAGS_nsinit_gid);
  const vector<string> kArgv = {
      FLAGS_nscon_path,                                  "create",
      Substitute("--nsinit_path=$0", FLAGS_nsinit_path), kInitUid,
      kInitGid, GetOutputFdArg(), SpecToStr(spec) };

  ExpectPipeOpen();
  ExpectDefaultSubprocessCalls(kArgv, 0);
  ExpectPipeOutput(kHandleStr);
  ExpectNsHandlerFactoryGetCall(kHandleStr);
  StatusOr<NamespaceController *> statusor = nscon_factory_->Create(spec, {});
  ASSERT_OK(statusor);
  delete statusor.ValueOrDie();
}

// Similar to above, but with custom flags.
TEST_F(NamespaceControllerFactoryImplTest, CreateCustomFlags) {
  NamespaceSpec spec;
  spec.mutable_pid();
  spec.mutable_mnt();
  spec.mutable_ipc();

  // Compose expected command with our custom flags.
  FLAGS_nsinit_path = "/usr/local/bin/custominit";
  FLAGS_nsinit_uid = 12345;
  FLAGS_nsinit_gid = 99999;
  const vector<string> kArgv = {
    FLAGS_nscon_path, "create",
    Substitute("--nsinit_path=$0", FLAGS_nsinit_path),
    "--nsinit_uid=12345", "--nsinit_gid=99999", GetOutputFdArg(),
    SpecToStr(spec) };

  ExpectPipeOpen();
  ExpectDefaultSubprocessCalls(kArgv, 0);
  ExpectPipeOutput(kHandleStr);
  ExpectNsHandlerFactoryGetCall(kHandleStr);

  StatusOr<NamespaceController *> statusor = nscon_factory_->Create(spec, {});
  ASSERT_OK(statusor);
  delete statusor.ValueOrDie();
}

// Simulate failure of nscon binary
TEST_F(NamespaceControllerFactoryImplTest, Create_NsconFailure) {
  NamespaceSpec spec;
  spec.mutable_pid();
  spec.mutable_mnt();
  spec.mutable_ipc();

  // Compose expected command with all default flags.
  FLAGS_nsinit_path = "/usr/local/bin/custominit";
  const string kCustomInitPath = Substitute("--nsinit_path=$0",
                                            FLAGS_nsinit_path);
  const string kInitUid = Substitute("--nsinit_uid=$0", FLAGS_nsinit_uid);
  const string kInitGid = Substitute("--nsinit_gid=$0", FLAGS_nsinit_gid);
  const vector<string> kArgv = {
    FLAGS_nscon_path, "create", kCustomInitPath,
    kInitUid, kInitGid, GetOutputFdArg(), SpecToStr(spec) };

  ExpectPipeOpen();
  ExpectDefaultSubprocessCalls(kArgv, 1);
  ExpectPipeOutput("error");

  EXPECT_ERROR_CODE(::util::error::INTERNAL, nscon_factory_->Create(spec, {}));
}

// Simulate failure of subprocess.Start()
TEST_F(NamespaceControllerFactoryImplTest, Create_SubprocessFailure) {
  NamespaceSpec spec;
  spec.mutable_pid();
  spec.mutable_mnt();
  spec.mutable_ipc();

  // Compose expected command with all default flags.
  const string kInitUid = Substitute("--nsinit_uid=$0", FLAGS_nsinit_uid);
  const string kInitGid = Substitute("--nsinit_gid=$0", FLAGS_nsinit_gid);
  const vector<string> kArgv = {
      FLAGS_nscon_path, "create",
      Substitute("--nsinit_path=$0", FLAGS_nsinit_path), kInitUid,
      kInitGid, GetOutputFdArg(), SpecToStr(spec)};
  ExpectPipeOpen();
  EXPECT_CALL(*mock_subprocess_, SetChannelAction(_, _)).Times(3);
  EXPECT_CALL(*mock_subprocess_, SetArgv(kArgv));
  EXPECT_CALL(*mock_subprocess_, Start()).WillOnce(Return(false));
  EXPECT_CALL(*mock_subprocess_, error_text()).WillOnce(Return("error"));

  EXPECT_ERROR_CODE(::util::error::INTERNAL, nscon_factory_->Create(spec, {}));
}

TEST_F(NamespaceControllerFactoryImplTest, Create_BadNsconOutput) {
  NamespaceSpec spec;
  spec.mutable_pid();
  spec.mutable_mnt();
  spec.mutable_ipc();

  // Compose expected command with all default flags.
  const string kInitUid = Substitute("--nsinit_uid=$0", FLAGS_nsinit_uid);
  const string kInitGid = Substitute("--nsinit_gid=$0", FLAGS_nsinit_gid);
  const vector<string> kArgv = {
      FLAGS_nscon_path, "create",
      Substitute("--nsinit_path=$0", FLAGS_nsinit_path), kInitUid,
      kInitGid, GetOutputFdArg(), SpecToStr(spec)};
  ExpectPipeOpen();
  ExpectDefaultSubprocessCalls(kArgv, 0);
  ExpectPipeOutput(kHandleStr);
  // nscon succeeds, but retuns junk or nshandle immediately becomes invalid.
  EXPECT_CALL(*mock_nsh_factory_, Get(kHandleStr))
      .WillOnce(Return(Status(::util::error::INTERNAL, "Invalid nshandle")));

  EXPECT_ERROR_CODE(::util::error::INTERNAL, nscon_factory_->Create(spec, {}));
}

TEST_F(NamespaceControllerFactoryImplTest, Create_CustomInit) {
  NamespaceSpec spec;
  spec.mutable_pid();
  spec.mutable_mnt();
  spec.mutable_ipc();

  // Compose expected command with all default flags.
  const string kInitUid = Substitute("--nsinit_uid=$0", FLAGS_nsinit_uid);
  const string kInitGid = Substitute("--nsinit_gid=$0", FLAGS_nsinit_gid);
  const vector<string> kArgv = {
      FLAGS_nscon_path, "create",
      Substitute("--nsinit_path=$0", FLAGS_nsinit_path), kInitUid, kInitGid,
      GetOutputFdArg(), SpecToStr(spec), "--",  "/custom/init", "arg1", "arg2"};
  const vector<string> kCustomInitArgv = { "/custom/init", "arg1", "arg2" };
  ExpectPipeOpen();
  ExpectDefaultSubprocessCalls(kArgv, 0);
  ExpectPipeOutput(kHandleStr);
  ExpectNsHandlerFactoryGetCall(kHandleStr);
  StatusOr<NamespaceController *> statusor =
      nscon_factory_->Create(spec, kCustomInitArgv);
  ASSERT_OK(statusor);
  delete statusor.ValueOrDie();
}

TEST_F(NamespaceControllerFactoryImplTest, Detect_Success) {
  const string kExpected = "1";
  const pid_t kPid = 123;

  EXPECT_CALL(*mock_ns_util_, GetNamespaceId(kPid, CLONE_NEWPID))
      .WillRepeatedly(Return(kExpected));

  StatusOr<string> statusor = nscon_factory_->GetNamespaceId(kPid);
  ASSERT_OK(statusor);
  EXPECT_EQ(kExpected, statusor.ValueOrDie());
}

TEST_F(NamespaceControllerFactoryImplTest, Detect_Fails) {
  const pid_t kPid = 123;

  EXPECT_CALL(*mock_ns_util_, GetNamespaceId(kPid, CLONE_NEWPID))
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED, nscon_factory_->GetNamespaceId(kPid).status());
}

///////////////////////////////////////////////////////////////////////////////
// NamespaceControllerImpl class tests

class NamespaceControllerImplTest : public ::testing::Test {
 public:
  NamespaceControllerImplTest() : pipefd_{10, 11} {}
  ~NamespaceControllerImplTest() {}

  void SetUp() {
    mock_nshandle_ = new ::testing::StrictMock<MockNsHandle>();
    mock_subprocess_ = new ::testing::StrictMock<MockSubProcess>();
    mock_subprocess_factory_.reset(
        NewPermanentCallback(&IdentitySubProcessFactory,
                             mock_subprocess_));
    nscon_.reset(new NamespaceControllerImpl(mock_nshandle_,
                                             mock_subprocess_factory_.get()));
  }

  string GetOutputFdArg() {
    return Substitute("--nscon_output_fd=$0", pipefd_[1]);
  }

  void ExpectDefaultSubprocessCalls(const vector<string> &argv,
                                    int error_code) {
    EXPECT_CALL(*mock_subprocess_, SetChannelAction(_, _)).Times(3);
    EXPECT_CALL(*mock_subprocess_, SetArgv(ContainerEq(argv)));
    EXPECT_CALL(*mock_subprocess_, Start()).WillOnce(Return(true));
    EXPECT_CALL(*mock_subprocess_, Wait()).WillOnce(Return(true));
    EXPECT_CALL(*mock_subprocess_, exit_code())
        .WillRepeatedly(Return(error_code));
  }

  void ExpectPipeOpen() {
    EXPECT_CALL(mock_libc_fs_api_.Mock(), Pipe(_))
        .WillOnce(DoAll(SetArrayArgument<0>(pipefd_, &pipefd_[2]), Return(0)));
    EXPECT_CALL(mock_libc_fs_api_.Mock(), Close(Eq(pipefd_[0])));
    EXPECT_CALL(mock_libc_fs_api_.Mock(), Close(Eq(pipefd_[1])));
  }

  void ExpectPipeOutput(const char *data) {
    char *tdata = const_cast<char *>(data);
    EXPECT_CALL(mock_libc_fs_api_.Mock(), Read(Eq(pipefd_[0]), _, _))
        .WillOnce(DoAll(SetArrayArgument<1>(tdata, tdata + strlen(tdata) + 1),
                        Return(10)));
  }

 protected:
  unique_ptr<NamespaceControllerImpl> nscon_;
  MockNsHandle *mock_nshandle_;
  MockSubProcess *mock_subprocess_;
  unique_ptr<SubProcessFactory> mock_subprocess_factory_;
  system_api::MockLibcProcessApiOverride libc_process_api_;
  ::system_api::MockLibcFsApiOverride mock_libc_fs_api_;
  int pipefd_[2];
};

TEST_F(NamespaceControllerImplTest, Run) {
  const vector<string> kCommand = {"/bin/ls", "-l"};
  const string kCommandPidStr = SimpleItoa(kPid);
  RunSpec run_spec;

  EXPECT_CALL(*mock_nshandle_, IsValid()).WillOnce(Return(true));
  EXPECT_CALL(*mock_nshandle_, ToString()).WillOnce(Return(kHandleStr));
  // Compose expected command with all default flags.
  vector<string> kArgv = {FLAGS_nscon_path, "run", GetOutputFdArg(), kHandleStr,
                          SpecToStr(run_spec), "--", "/bin/ls", "-l"};
  ExpectPipeOpen();
  ExpectDefaultSubprocessCalls(kArgv, 0);
  ExpectPipeOutput(kCommandPidStr.data());

  StatusOr<pid_t> statusor = nscon_->Run(kCommand, run_spec);
  ASSERT_OK(statusor);
  EXPECT_EQ(kPid, statusor.ValueOrDie());
}

TEST_F(NamespaceControllerImplTest, RunWithRunSpec) {
  const vector<string> kCommand = {"/bin/ls", "-l"};
  const string kCommandPidStr = SimpleItoa(kPid);
  RunSpec run_spec;
  run_spec.set_uid(99);
  run_spec.set_gid(99);

  EXPECT_CALL(*mock_nshandle_, IsValid()).WillOnce(Return(true));
  EXPECT_CALL(*mock_nshandle_, ToString()).WillOnce(Return(kHandleStr));
  // Compose expected command with all default flags.
  vector<string> kArgv = {FLAGS_nscon_path, "run", GetOutputFdArg(), kHandleStr,
                          SpecToStr(run_spec), "--", "/bin/ls", "-l"};
  ExpectPipeOpen();
  ExpectDefaultSubprocessCalls(kArgv, 0);
  ExpectPipeOutput(kCommandPidStr.data());

  StatusOr<pid_t> statusor = nscon_->Run(kCommand, run_spec);
  ASSERT_OK(statusor);
  EXPECT_EQ(kPid, statusor.ValueOrDie());
}

TEST_F(NamespaceControllerImplTest, Run_InvalidNshandle) {
  const vector<string> kCommand = {"/bin/ls", "-l"};
  const string kCommandPidStr = SimpleItoa(kPid);
  RunSpec run_spec;

  EXPECT_CALL(*mock_nshandle_, IsValid()).WillOnce(Return(false));
  EXPECT_CALL(*mock_nshandle_, ToString()).WillOnce(Return(kHandleStr));
  EXPECT_ERROR_CODE(::util::error::INTERNAL, nscon_->Run(kCommand, run_spec));
}

TEST_F(NamespaceControllerImplTest, Run_SubprocessFailure) {
  const vector<string> kCommand = {"/bin/ls", "-l"};
  RunSpec run_spec;

  EXPECT_CALL(*mock_nshandle_, IsValid()).WillOnce(Return(true));
  EXPECT_CALL(*mock_nshandle_, ToString()).WillOnce(Return(kHandleStr));
  // Compose expected command with all default flags.
  vector<string> kArgv = {FLAGS_nscon_path, "run", GetOutputFdArg(), kHandleStr,
                          SpecToStr(run_spec), "--", "/bin/ls", "-l"};
  ExpectPipeOpen();
  EXPECT_CALL(*mock_subprocess_, SetChannelAction(_, _)).Times(3);
  EXPECT_CALL(*mock_subprocess_, SetArgv(kArgv));
  EXPECT_CALL(*mock_subprocess_, Start()).WillOnce(Return(false));
  EXPECT_CALL(*mock_subprocess_, error_text()).WillOnce(Return("error"));

  EXPECT_ERROR_CODE(::util::error::INTERNAL, nscon_->Run(kCommand, run_spec));
}

TEST_F(NamespaceControllerImplTest, Run_NsconFailure) {
  const vector<string> kCommand = {"/bin/ls", "-l"};
  RunSpec run_spec;

  EXPECT_CALL(*mock_nshandle_, IsValid()).WillOnce(Return(true));
  EXPECT_CALL(*mock_nshandle_, ToString()).WillOnce(Return(kHandleStr));
  // Compose expected command with all default flags.
  vector<string> kArgv = {FLAGS_nscon_path, "run", GetOutputFdArg(), kHandleStr,
                          SpecToStr(run_spec), "--", "/bin/ls", "-l"};
  ExpectPipeOpen();
  ExpectDefaultSubprocessCalls(kArgv, 1);
  ExpectPipeOutput("Error");

  EXPECT_ERROR_CODE(::util::error::INTERNAL, nscon_->Run(kCommand, run_spec));
}

TEST_F(NamespaceControllerImplTest, Run_BadNsconOutput) {
  const vector<string> kCommand = {"/bin/ls", "-l"};
  const char *kBadPidStr = "bad 1234 pid";
  RunSpec run_spec;

  EXPECT_CALL(*mock_nshandle_, IsValid()).WillOnce(Return(true));
  EXPECT_CALL(*mock_nshandle_, ToString()).WillOnce(Return(kHandleStr));
  // Compose expected command with all default flags.
  vector<string> kArgv = {FLAGS_nscon_path, "run", GetOutputFdArg(), kHandleStr,
                          SpecToStr(run_spec), "--", "/bin/ls", "-l"};
  ExpectPipeOpen();
  ExpectDefaultSubprocessCalls(kArgv, 0);
  ExpectPipeOutput(kBadPidStr);

  EXPECT_ERROR_CODE(::util::error::INTERNAL, nscon_->Run(kCommand, run_spec));
}

TEST_F(NamespaceControllerImplTest, Exec) {
  const vector<string> kCommand = {"/bin/ls", "-l"};
  const vector<string> kExpected = {};

  EXPECT_CALL(*mock_nshandle_, IsValid()).WillOnce(Return(true));
  EXPECT_CALL(*mock_nshandle_, ToString()).WillOnce(Return(kHandleStr));
  // TODO(vmarmol): Match more tightly.
  EXPECT_CALL(libc_process_api_.Mock(), Execve(StrEq(FLAGS_nscon_path), _, _))
      .WillOnce(Return(0));

  EXPECT_ERROR_CODE(::util::error::INTERNAL, nscon_->Exec(kCommand));
}

TEST_F(NamespaceControllerImplTest, Exec_InvalidHandle) {
  const vector<string> kCommand = {"/bin/ls", "-l"};

  EXPECT_CALL(*mock_nshandle_, IsValid()).WillOnce(Return(false));
  EXPECT_CALL(*mock_nshandle_, ToString()).WillOnce(Return(kHandleStr));

  EXPECT_ERROR_CODE(::util::error::INTERNAL, nscon_->Exec(kCommand));
}

TEST_F(NamespaceControllerImplTest, Exec_ExecFails) {
  const vector<string> kCommand = {"/bin/ls", "-l"};

  EXPECT_CALL(*mock_nshandle_, IsValid()).WillOnce(Return(true));
  EXPECT_CALL(*mock_nshandle_, ToString()).WillOnce(Return(kHandleStr));
  EXPECT_CALL(libc_process_api_.Mock(), Execve(_, _, _))
      .WillOnce(Return(1));

  EXPECT_ERROR_CODE(::util::error::INTERNAL, nscon_->Exec(kCommand));
}

TEST_F(NamespaceControllerImplTest, Update) {
  NamespaceSpec spec;
  spec.mutable_pid();
  spec.mutable_mnt();
  spec.mutable_ipc();

  EXPECT_CALL(*mock_nshandle_, IsValid()).WillOnce(Return(true));
  EXPECT_CALL(*mock_nshandle_, ToString()).WillOnce(Return(kHandleStr));
  // Compose expected nscon command.
  vector<string> kArgv = {FLAGS_nscon_path, "update",  GetOutputFdArg(),
                          kHandleStr, SpecToStr(spec)};

  ExpectPipeOpen();
  ExpectDefaultSubprocessCalls(kArgv, 0);
  ExpectPipeOutput("");

  ASSERT_OK(nscon_->Update(spec));
}

TEST_F(NamespaceControllerImplTest, Update_InvalidHandle) {
  NamespaceSpec spec;
  spec.mutable_pid();
  spec.mutable_mnt();
  spec.mutable_ipc();

  EXPECT_CALL(*mock_nshandle_, IsValid()).WillOnce(Return(false));
  EXPECT_CALL(*mock_nshandle_, ToString()).WillOnce(Return(kHandleStr));

  EXPECT_ERROR_CODE(::util::error::INTERNAL, nscon_->Update(spec));
}

TEST_F(NamespaceControllerImplTest, Update_SubprocessStartFailure) {
  NamespaceSpec spec;
  spec.mutable_pid();
  spec.mutable_mnt();
  spec.mutable_ipc();

  EXPECT_CALL(*mock_nshandle_, IsValid()).WillOnce(Return(true));
  EXPECT_CALL(*mock_nshandle_, ToString()).WillOnce(Return(kHandleStr));
  // Compose expected nscon command.
  vector<string> kArgv = {FLAGS_nscon_path, "update", GetOutputFdArg(),
                          kHandleStr, SpecToStr(spec)};
  ExpectPipeOpen();
  EXPECT_CALL(*mock_subprocess_, SetChannelAction(_, _)).Times(3);
  EXPECT_CALL(*mock_subprocess_, SetArgv(kArgv));
  EXPECT_CALL(*mock_subprocess_, Start()).WillOnce(Return(false));
  EXPECT_CALL(*mock_subprocess_, error_text()).WillOnce(Return("error"));

  EXPECT_ERROR_CODE(::util::error::INTERNAL, nscon_->Update(spec));
}

TEST_F(NamespaceControllerImplTest, Update_NsconFailure) {
  NamespaceSpec spec;
  spec.mutable_pid();
  spec.mutable_mnt();
  spec.mutable_ipc();

  EXPECT_CALL(*mock_nshandle_, IsValid()).WillOnce(Return(true));
  EXPECT_CALL(*mock_nshandle_, ToString()).WillOnce(Return(kHandleStr));
  // Compose expected nscon command.
  vector<string> kArgv = {FLAGS_nscon_path, "update", GetOutputFdArg(),
                          kHandleStr, SpecToStr(spec)};

  ExpectPipeOpen();
  ExpectDefaultSubprocessCalls(kArgv, 1);
  ExpectPipeOutput("nscon failed");

  EXPECT_ERROR_CODE(::util::error::INTERNAL, nscon_->Update(spec));
}

TEST_F(NamespaceControllerImplTest, Destroy) {
  EXPECT_CALL(*mock_nshandle_, IsValid()).WillOnce(Return(true));
  EXPECT_CALL(*mock_nshandle_, ToPid()).WillOnce(Return(kPid));
  EXPECT_CALL(libc_process_api_.Mock(), Kill(kPid, SIGKILL))
      .WillOnce(Return(0));
  ASSERT_OK(nscon_->Destroy());
}

TEST_F(NamespaceControllerImplTest, Destroy_InvalidNshandle) {
  EXPECT_CALL(*mock_nshandle_, IsValid()).WillOnce(Return(false));
  EXPECT_CALL(*mock_nshandle_, ToString()).WillOnce(Return(kHandleStr));
  EXPECT_ERROR_CODE(::util::error::INTERNAL, nscon_->Destroy());
}

TEST_F(NamespaceControllerImplTest, Destroy_KillFailure) {
  EXPECT_CALL(*mock_nshandle_, IsValid()).WillOnce(Return(true));
  EXPECT_CALL(*mock_nshandle_, ToPid()).WillOnce(Return(kPid));
  EXPECT_CALL(libc_process_api_.Mock(), Kill(kPid, SIGKILL))
      .WillOnce(Return(-1));
  EXPECT_ERROR_CODE(::util::error::INTERNAL, nscon_->Destroy());
}

TEST_F(NamespaceControllerImplTest, IsValid) {
  EXPECT_CALL(*mock_nshandle_, IsValid()).WillOnce(Return(true));
  EXPECT_TRUE(nscon_->IsValid());
}

TEST_F(NamespaceControllerImplTest, InValid) {
  EXPECT_CALL(*mock_nshandle_, IsValid()).WillOnce(Return(false));
  EXPECT_FALSE(nscon_->IsValid());
}

TEST_F(NamespaceControllerImplTest, GetHandleString) {
  EXPECT_CALL(*mock_nshandle_, ToString()).WillOnce(Return(kHandleStr));
  EXPECT_EQ(kHandleStr, nscon_->GetHandleString());
}

TEST_F(NamespaceControllerImplTest, GetPid) {
  EXPECT_CALL(*mock_nshandle_, ToPid()).WillOnce(Return(kPid));
  EXPECT_EQ(kPid, nscon_->GetPid());
}

}  // namespace nscon
}  // namespace containers
