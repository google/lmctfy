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
using ::testing::NotNull;
using ::testing::Return;
using ::testing::SetArgumentPointee;
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

static const string SpecToStr(const NamespaceSpec &spec) {
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

 protected:
  unique_ptr<NamespaceControllerFactoryImpl> nscon_factory_;
  MockNsHandle *mock_nshandle_;
  MockNsHandleFactory *mock_nsh_factory_;
  MockSubProcess *mock_subprocess_;
  MockNsUtil *mock_ns_util_;
  google::FlagSaver flag_saver_;
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
  const vector<string> kArgv = { "/usr/local/bin/nscon", "create",
                                 "--nsinit_path=/usr/local/bin/nsinit",
                                 kInitUid, kInitGid, SpecToStr(spec) };
  EXPECT_CALL(*mock_subprocess_, SetChannelAction(_, _)).Times(2);
  EXPECT_CALL(*mock_subprocess_, SetArgv(kArgv));
  EXPECT_CALL(*mock_subprocess_, Start()).WillOnce(Return(true));
  EXPECT_CALL(*mock_subprocess_, Communicate(NotNull(), NotNull()))
      .WillOnce(DoAll(SetArgumentPointee<0>(kHandleStr), Return(0)));
  EXPECT_CALL(*mock_nsh_factory_, Get(kHandleStr))
      .WillOnce(Return(mock_nshandle_));
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
  const vector<string> kArgv = { "/usr/local/bin/nscon", "create",
                                 "--nsinit_path=/usr/local/bin/custominit",
                                 "--nsinit_uid=12345",
                                 "--nsinit_gid=99999",
                                 SpecToStr(spec) };
  EXPECT_CALL(*mock_subprocess_, SetChannelAction(_, _)).Times(2);
  EXPECT_CALL(*mock_subprocess_, SetArgv(kArgv));
  EXPECT_CALL(*mock_subprocess_, Start()).WillOnce(Return(true));
  EXPECT_CALL(*mock_subprocess_, Communicate(NotNull(), NotNull()))
      .WillOnce(DoAll(SetArgumentPointee<0>(kHandleStr), Return(0)));
  EXPECT_CALL(*mock_nsh_factory_, Get(kHandleStr))
      .WillOnce(Return(mock_nshandle_));

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
  const string kInitUid = Substitute("--nsinit_uid=$0", FLAGS_nsinit_uid);
  const string kInitGid = Substitute("--nsinit_gid=$0", FLAGS_nsinit_gid);
  const vector<string> kArgv = { "/usr/local/bin/nscon", "create",
                                 "--nsinit_path=/usr/local/bin/nsinit",
                                 kInitUid, kInitGid, SpecToStr(spec) };
  EXPECT_CALL(*mock_subprocess_, SetChannelAction(_, _)).Times(2);
  EXPECT_CALL(*mock_subprocess_, SetArgv(kArgv));
  EXPECT_CALL(*mock_subprocess_, Start()).WillOnce(Return(true));
  // Return non-zero exit status from nscon.
  EXPECT_CALL(*mock_subprocess_, Communicate(NotNull(), NotNull()))
      .WillOnce(Return(1));
  EXPECT_CALL(*mock_subprocess_, exit_code()).WillOnce(Return(1));

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
  const vector<string> kArgv = { "/usr/local/bin/nscon", "create",
                                 "--nsinit_path=/usr/local/bin/nsinit",
                                 kInitUid, kInitGid, SpecToStr(spec) };
  EXPECT_CALL(*mock_subprocess_, SetChannelAction(_, _)).Times(2);
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
  const vector<string> kArgv = { "/usr/local/bin/nscon", "create",
                                 "--nsinit_path=/usr/local/bin/nsinit",
                                 kInitUid, kInitGid, SpecToStr(spec) };
  EXPECT_CALL(*mock_subprocess_, SetChannelAction(_, _)).Times(2);
  EXPECT_CALL(*mock_subprocess_, SetArgv(kArgv));
  EXPECT_CALL(*mock_subprocess_, Start()).WillOnce(Return(true));
  EXPECT_CALL(*mock_subprocess_, Communicate(NotNull(), NotNull()))
      .WillOnce(DoAll(SetArgumentPointee<0>(kHandleStr), Return(0)));
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
  const vector<string> kArgv = { "/usr/local/bin/nscon", "create",
                                 "--nsinit_path=/usr/local/bin/nsinit",
                                 kInitUid, kInitGid, SpecToStr(spec),
                                "--", "/custom/init", "arg1", "arg2"};
  const vector<string> kCustomInitArgv = { "/custom/init", "arg1", "arg2" };
  EXPECT_CALL(*mock_subprocess_, SetChannelAction(_, _)).Times(2);
  EXPECT_CALL(*mock_subprocess_, SetArgv(kArgv));
  EXPECT_CALL(*mock_subprocess_, Start()).WillOnce(Return(true));
  EXPECT_CALL(*mock_subprocess_, Communicate(NotNull(), NotNull()))
      .WillOnce(DoAll(SetArgumentPointee<0>(kHandleStr), Return(0)));
  EXPECT_CALL(*mock_nsh_factory_, Get(kHandleStr))
      .WillOnce(Return(mock_nshandle_));
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
  void SetUp() {
    mock_nshandle_ = new ::testing::StrictMock<MockNsHandle>();
    mock_subprocess_ = new ::testing::StrictMock<MockSubProcess>();
    mock_subprocess_factory_.reset(
        NewPermanentCallback(&IdentitySubProcessFactory,
                             mock_subprocess_));
    nscon_.reset(new NamespaceControllerImpl(mock_nshandle_,
                                             mock_subprocess_factory_.get()));
  }

 protected:
  unique_ptr<NamespaceControllerImpl> nscon_;
  MockNsHandle *mock_nshandle_;
  MockSubProcess *mock_subprocess_;
  unique_ptr<SubProcessFactory> mock_subprocess_factory_;
  system_api::MockLibcProcessApiOverride libc_process_api_;
};

TEST_F(NamespaceControllerImplTest, Run) {
  const vector<string> kCommand = {"/bin/ls", "-l"};
  const string kCommandPidStr = SimpleItoa(kPid);

  EXPECT_CALL(*mock_nshandle_, IsValid()).WillOnce(Return(true));
  EXPECT_CALL(*mock_nshandle_, ToString()).WillOnce(Return(kHandleStr));
  // Compose expected command with all default flags.
  vector<string> kArgv = { "/usr/local/bin/nscon", "run", kHandleStr, "--",
                           "/bin/ls", "-l" };
  EXPECT_CALL(*mock_subprocess_, SetChannelAction(_, _)).Times(2);
  EXPECT_CALL(*mock_subprocess_, SetArgv(kArgv));
  EXPECT_CALL(*mock_subprocess_, Start()).WillOnce(Return(true));
  EXPECT_CALL(*mock_subprocess_, Communicate(NotNull(), NotNull()))
      .WillOnce(DoAll(SetArgumentPointee<0>(kCommandPidStr), Return(0)));

  StatusOr<pid_t> statusor = nscon_->Run(kCommand);
  ASSERT_OK(statusor);
  EXPECT_EQ(kPid, statusor.ValueOrDie());
}

TEST_F(NamespaceControllerImplTest, Run_InvalidNshandle) {
  const vector<string> kCommand = {"/bin/ls", "-l"};
  const string kCommandPidStr = SimpleItoa(kPid);

  EXPECT_CALL(*mock_nshandle_, IsValid()).WillOnce(Return(false));
  EXPECT_CALL(*mock_nshandle_, ToString()).WillOnce(Return(kHandleStr));
  EXPECT_ERROR_CODE(::util::error::INTERNAL, nscon_->Run(kCommand));
}

TEST_F(NamespaceControllerImplTest, Run_SubprocessFailure) {
  const vector<string> kCommand = {"/bin/ls", "-l"};

  EXPECT_CALL(*mock_nshandle_, IsValid()).WillOnce(Return(true));
  EXPECT_CALL(*mock_nshandle_, ToString()).WillOnce(Return(kHandleStr));
  // Compose expected command with all default flags.
  vector<string> kArgv = { "/usr/local/bin/nscon", "run", kHandleStr, "--",
                           "/bin/ls", "-l" };
  EXPECT_CALL(*mock_subprocess_, SetChannelAction(_, _)).Times(2);
  EXPECT_CALL(*mock_subprocess_, SetArgv(kArgv));
  EXPECT_CALL(*mock_subprocess_, Start()).WillOnce(Return(false));
  EXPECT_CALL(*mock_subprocess_, error_text()).WillOnce(Return("error"));

  EXPECT_ERROR_CODE(::util::error::INTERNAL, nscon_->Run(kCommand));
}

TEST_F(NamespaceControllerImplTest, Run_NsconFailure) {
  const vector<string> kCommand = {"/bin/ls", "-l"};

  EXPECT_CALL(*mock_nshandle_, IsValid()).WillOnce(Return(true));
  EXPECT_CALL(*mock_nshandle_, ToString()).WillOnce(Return(kHandleStr));
  // Compose expected command with all default flags.
  vector<string> kArgv = { "/usr/local/bin/nscon", "run", kHandleStr, "--",
                           "/bin/ls", "-l" };
  EXPECT_CALL(*mock_subprocess_, SetChannelAction(_, _)).Times(2);
  EXPECT_CALL(*mock_subprocess_, SetArgv(kArgv));
  EXPECT_CALL(*mock_subprocess_, Start()).WillOnce(Return(true));
  EXPECT_CALL(*mock_subprocess_, Communicate(NotNull(), NotNull()))
      .WillOnce(Return(1));
  EXPECT_CALL(*mock_subprocess_, exit_code()).WillOnce(Return(1));

  EXPECT_ERROR_CODE(::util::error::INTERNAL, nscon_->Run(kCommand));
}

TEST_F(NamespaceControllerImplTest, Run_BadNsconOutput) {
  const vector<string> kCommand = {"/bin/ls", "-l"};
  const string kBadPidStr = "bad 1234 pid";

  EXPECT_CALL(*mock_nshandle_, IsValid()).WillOnce(Return(true));
  EXPECT_CALL(*mock_nshandle_, ToString()).WillOnce(Return(kHandleStr));
  // Compose expected command with all default flags.
  vector<string> kArgv = { "/usr/local/bin/nscon", "run", kHandleStr, "--",
                           "/bin/ls", "-l" };
  EXPECT_CALL(*mock_subprocess_, SetChannelAction(_, _)).Times(2);
  EXPECT_CALL(*mock_subprocess_, SetArgv(kArgv));
  EXPECT_CALL(*mock_subprocess_, Start()).WillOnce(Return(true));
  EXPECT_CALL(*mock_subprocess_, Communicate(NotNull(), NotNull()))
      .WillOnce(DoAll(SetArgumentPointee<0>(kBadPidStr), Return(0)));

  EXPECT_ERROR_CODE(::util::error::INTERNAL, nscon_->Run(kCommand));
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
  vector<string> kArgv = { "/usr/local/bin/nscon", "update", kHandleStr,
                           SpecToStr(spec) };

  EXPECT_CALL(*mock_subprocess_, SetChannelAction(_, _)).Times(2);
  EXPECT_CALL(*mock_subprocess_, SetArgv(kArgv));
  EXPECT_CALL(*mock_subprocess_, Start()).WillOnce(Return(true));
  EXPECT_CALL(*mock_subprocess_, Communicate(NotNull(), NotNull()))
      .WillOnce(Return(0));

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
  vector<string> kArgv = { "/usr/local/bin/nscon", "update", kHandleStr,
                           SpecToStr(spec) };

  EXPECT_CALL(*mock_subprocess_, SetChannelAction(_, _)).Times(2);
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
  vector<string> kArgv = { "/usr/local/bin/nscon", "update", kHandleStr,
                           SpecToStr(spec) };

  EXPECT_CALL(*mock_subprocess_, SetChannelAction(_, _)).Times(2);
  EXPECT_CALL(*mock_subprocess_, SetArgv(kArgv));
  EXPECT_CALL(*mock_subprocess_, Start()).WillOnce(Return(true));

  const char *kNsconStderr = "nscon failed";
  EXPECT_CALL(*mock_subprocess_, Communicate(NotNull(), NotNull()))
      .WillOnce(DoAll(SetArgumentPointee<1>(kNsconStderr), Return(1)));
  EXPECT_CALL(*mock_subprocess_, exit_code()).WillOnce(Return(1));

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
