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

#include "nscon/namespace_controller_cli.h"

#include <map>

#include "gflags/gflags.h"
#include "nscon/configurator/ns_configurator_mock.h"
#include "nscon/configurator/ns_configurator_factory_mock.h"
#include "nscon/ns_handle_mock.h"
#include "nscon/ns_util_mock.h"
#include "nscon/process_launcher_mock.h"
#include "util/errors_test_util.h"
#include "system_api/libc_fs_api_test_util.h"
#include "system_api/libc_process_api_test_util.h"
#include "strings/substitute.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

DECLARE_string(nsinit_path);
DECLARE_uint64(nsinit_uid);
DECLARE_uint64(nsinit_gid);

using ::std::map;
using ::std::unique_ptr;
using ::std::vector;
using ::strings::Substitute;
using ::testing::DoAll;
#include "util/testing/equals_initialized_proto.h"
using ::testing::EqualsInitializedProto;
using ::testing::HasSubstr;
using ::testing::Invoke;
using ::testing::NotNull;
using ::testing::Return;
using ::testing::SetArgumentPointee;
using ::testing::SetErrnoAndReturn;
using ::testing::SizeIs;
using ::testing::StrEq;
using ::testing::_;
using ::util::error::INVALID_ARGUMENT;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace nscon {

// These should not collide with clone-flag values.
static const int FSCONFIG = 0;
static const int MACHINECONFIG = 1;

static StatusOr<const char*> RealNsCloneFlagToName(int clone_flag) {
  switch (clone_flag) {
    case CLONE_NEWUSER:
      return "user";
    case CLONE_NEWPID:
      return "pid";
    case CLONE_NEWNS:
      return "mnt";
    case CLONE_NEWIPC:
      return "ipc";
    case CLONE_NEWNET:
      return "net";
    case CLONE_NEWUTS:
      return "uts";
    default:
      return Status(INVALID_ARGUMENT, "Invalid namespace flag");
  }
}

class NamespaceControllerCliTest : public ::testing::Test {
 public:
  void SetUp() {
    mock_nsh_factory_ = new ::testing::StrictMock<MockNsHandleFactory>();
    mock_nshandle_ = new ::testing::StrictMock<MockNsHandle>();
    mock_ns_util_ = new ::testing::StrictMock<MockNsUtil>();
    mock_pl_ = new ::testing::StrictMock<MockProcessLauncher>();
    mock_config_factory_ =
        new ::testing::StrictMock<MockNsConfiguratorFactory>();
    nscon_.reset(new NamespaceControllerCli(mock_nsh_factory_, mock_ns_util_,
                                            mock_pl_, mock_config_factory_));
  }

  void SetSupportedNamespaces(const vector<int> &namespaces) {
    // Exclusively support only the specified namespaces.
    EXPECT_CALL(*mock_ns_util_, IsNamespaceSupported(_))
        .WillRepeatedly(Return(false));
    for (auto ns : namespaces) {
      EXPECT_CALL(*mock_ns_util_, IsNamespaceSupported(ns))
          .WillRepeatedly(Return(true));
    }
    EXPECT_CALL(*mock_ns_util_, NsCloneFlagToName(_))
        .WillRepeatedly(Invoke(&RealNsCloneFlagToName));
  }

  void SetSupportedConfigurators(const vector<int> &namespaces,
                                 map<int, MockNsConfigurator *> *mock_configs,
                                 bool add_filesys_config) {
    // Exclusively support configurators for specified namespaces.
    EXPECT_CALL(*mock_config_factory_, Get(_))
        .WillRepeatedly(Return(Status(::util::error::NOT_FOUND,
                                      "Configurator not available")));
    // First add the filesystem configurator.
    if (add_filesys_config) {
      MockNsConfigurator *mock_config =
          new ::testing::StrictMock<MockNsConfigurator>();
      EXPECT_CALL(*mock_config_factory_, GetFilesystemConfigurator())
          .WillRepeatedly(Return(mock_config));
      EXPECT_CALL(*mock_config, SetupOutsideNamespace(_, kPid_))
          .WillRepeatedly(Return(Status::OK));
      EXPECT_CALL(*mock_config, SetupInsideNamespace(_))
          .WillRepeatedly(Return(Status::OK));
      (*mock_configs)[FSCONFIG] = mock_config;
    }

    for (auto ns : namespaces) {
      MockNsConfigurator *mock_config =
          new ::testing::StrictMock<MockNsConfigurator>(ns);
      EXPECT_CALL(*mock_config_factory_, Get(ns))
          .WillRepeatedly(Return(mock_config));
      EXPECT_CALL(*mock_config, SetupOutsideNamespace(_, kPid_))
          .WillRepeatedly(Return(Status::OK));
      EXPECT_CALL(*mock_config, SetupInsideNamespace(_))
          .WillRepeatedly(Return(Status::OK));
      (*mock_configs)[ns] = mock_config;
    }
  }

  void AddMachineConfigurator(map<int, MockNsConfigurator *> *mock_configs) {
    MockNsConfigurator *mock_config =
        new ::testing::StrictMock<MockNsConfigurator>();
    EXPECT_CALL(*mock_config_factory_, GetMachineConfigurator())
        .WillRepeatedly(Return(mock_config));
    EXPECT_CALL(*mock_config, SetupOutsideNamespace(_, kPid_))
        .WillRepeatedly(Return(Status::OK));
    EXPECT_CALL(*mock_config, SetupInsideNamespace(_))
        .WillRepeatedly(Return(Status::OK));
    (*mock_configs)[MACHINECONFIG] = mock_config;
  }

 protected:
  MockNsHandleFactory *mock_nsh_factory_;
  MockNsHandle *mock_nshandle_;
  MockNsUtil *mock_ns_util_;
  MockProcessLauncher *mock_pl_;
  MockNsConfiguratorFactory *mock_config_factory_;
  unique_ptr<NamespaceControllerCli> nscon_;
  const pid_t kPid_ = 9999;
  const string kHandleStr = "c3735928559-9999";
  system_api::MockLibcFsApiOverride libc_fs_api_;
  system_api::MockLibcProcessApiOverride libc_process_api_;

  gflags::FlagSaver flag_saver_;
};

TEST_F(NamespaceControllerCliTest, New) {
  // Mock out the namespace detection.
  EXPECT_CALL(libc_fs_api_.Mock(), LStat(_, NotNull()))
      .WillRepeatedly(Return(0));

  StatusOr<NamespaceControllerCli *> statusor = NamespaceControllerCli::New();
  ASSERT_OK(statusor);
  delete statusor.ValueOrDie();
}

TEST_F(NamespaceControllerCliTest, Create) {
  NamespaceSpec spec;
  spec.mutable_pid();
  spec.mutable_mnt();
  spec.mutable_ipc();
  RunSpec *run_spec = spec.mutable_run_spec();
  run_spec->mutable_console()->set_slave_pty("10");

  const string kInitUid = Substitute("--uid=$0", FLAGS_nsinit_uid);
  const string kInitGid = Substitute("--gid=$0", FLAGS_nsinit_gid);
  const vector<string> kArgv = { FLAGS_nsinit_path, kInitUid, kInitGid };
  const vector<int> kNamespaces = { CLONE_NEWIPC, CLONE_NEWPID, CLONE_NEWNS };
  SetSupportedNamespaces(kNamespaces);
  map<int, MockNsConfigurator *> mock_configs;
  SetSupportedConfigurators(kNamespaces, &mock_configs, true);
  AddMachineConfigurator(&mock_configs);

  EXPECT_CALL(*mock_pl_,
              NewNsProcess(kArgv, kNamespaces, SizeIs(5),
                           EqualsInitializedProto(spec),
                           EqualsInitializedProto(*run_spec)))
      .WillOnce(Return(kPid_));
  EXPECT_CALL(*mock_nsh_factory_, Get(kPid_)).WillOnce(Return(mock_nshandle_));
  EXPECT_CALL(*mock_nshandle_, ToString()).WillOnce(Return(kHandleStr));
  StatusOr<const string> statusor = nscon_->Create(spec, {});
  ASSERT_OK(statusor);
  EXPECT_EQ(kHandleStr, statusor.ValueOrDie());
}

TEST_F(NamespaceControllerCliTest, Create_CustomFlags) {
  NamespaceSpec spec;
  spec.mutable_pid();
  spec.mutable_mnt();
  spec.mutable_ipc();
  RunSpec run_spec;

  FLAGS_nsinit_path = "/usr/local/bin/custominit";
  FLAGS_nsinit_uid = 12345;
  FLAGS_nsinit_gid = 99999;
  const vector<string> kArgv = { "/usr/local/bin/custominit", "--uid=12345",
                                 "--gid=99999" };
  const vector<int> kNamespaces = { CLONE_NEWIPC, CLONE_NEWPID, CLONE_NEWNS };
  SetSupportedNamespaces(kNamespaces);
  map<int, MockNsConfigurator *> mock_configs;
  SetSupportedConfigurators(kNamespaces, &mock_configs, true);
  AddMachineConfigurator(&mock_configs);

  EXPECT_CALL(*mock_pl_,
              NewNsProcess(kArgv, kNamespaces, SizeIs(5),
                           EqualsInitializedProto(spec),
                           EqualsInitializedProto(run_spec)))
      .WillOnce(Return(kPid_));
  EXPECT_CALL(*mock_nsh_factory_, Get(kPid_)).WillOnce(Return(mock_nshandle_));
  EXPECT_CALL(*mock_nshandle_, ToString()).WillOnce(Return(kHandleStr));
  StatusOr<const string> statusor = nscon_->Create(spec, {});
  ASSERT_OK(statusor);
  EXPECT_EQ(kHandleStr, statusor.ValueOrDie());
}

TEST_F(NamespaceControllerCliTest, Create_UnsupportedNamespace) {
  NamespaceSpec spec;
  spec.mutable_pid();
  spec.mutable_mnt();
  spec.mutable_ipc();

  const string kInitUid = Substitute("--uid=$0", FLAGS_nsinit_uid);
  const string kInitGid = Substitute("--gid=$0", FLAGS_nsinit_gid);
  const vector<string> kArgv = { FLAGS_nsinit_path, kInitUid, kInitGid };
  const vector<int> kNamespaces = { CLONE_NEWIPC, CLONE_NEWPID, CLONE_NEWNS };
  // Assume CLONE_NEWNS is not supported.
  SetSupportedNamespaces({ CLONE_NEWIPC, CLONE_NEWPID });
  map<int, MockNsConfigurator *> mock_configs;
  SetSupportedConfigurators({CLONE_NEWIPC, CLONE_NEWPID }, &mock_configs, true);

  EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT, nscon_->Create(spec, {}));
}

TEST_F(NamespaceControllerCliTest, Create_ProcessLauncherFailure) {
  NamespaceSpec spec;
  spec.mutable_pid();
  spec.mutable_mnt();
  spec.mutable_ipc();
  RunSpec run_spec;

  const string kInitUid = Substitute("--uid=$0", FLAGS_nsinit_uid);
  const string kInitGid = Substitute("--gid=$0", FLAGS_nsinit_gid);
  const vector<string> kArgv = { FLAGS_nsinit_path, kInitUid, kInitGid };
  const vector<int> kNamespaces = { CLONE_NEWIPC, CLONE_NEWPID, CLONE_NEWNS };
  SetSupportedNamespaces(kNamespaces);
  map<int, MockNsConfigurator *> mock_configs;
  SetSupportedConfigurators(kNamespaces, &mock_configs, true);
  AddMachineConfigurator(&mock_configs);

  EXPECT_CALL(*mock_pl_,
              NewNsProcess(kArgv, kNamespaces, SizeIs(5),
                           EqualsInitializedProto(spec),
                           EqualsInitializedProto(run_spec)))
      .WillOnce(Return(kPid_));
  EXPECT_CALL(*mock_nsh_factory_, Get(kPid_))
      .WillOnce(Return(Status(::util::error::INTERNAL, "nshandle failed")));
  EXPECT_ERROR_CODE(::util::error::INTERNAL, nscon_->Create(spec, {}));
}

TEST_F(NamespaceControllerCliTest, Create_NsHandleFailure) {
  NamespaceSpec spec;
  spec.mutable_pid();
  spec.mutable_mnt();
  spec.mutable_ipc();
  RunSpec run_spec;

  const string kInitUid = Substitute("--uid=$0", FLAGS_nsinit_uid);
  const string kInitGid = Substitute("--gid=$0", FLAGS_nsinit_gid);
  const vector<string> kArgv = { FLAGS_nsinit_path, kInitUid, kInitGid };
  const vector<int> kNamespaces = { CLONE_NEWIPC, CLONE_NEWPID, CLONE_NEWNS };
  SetSupportedNamespaces(kNamespaces);
  map<int, MockNsConfigurator *> mock_configs;
  SetSupportedConfigurators(kNamespaces, &mock_configs, true);
  AddMachineConfigurator(&mock_configs);

  EXPECT_CALL(*mock_pl_,
              NewNsProcess(kArgv, kNamespaces, SizeIs(5),
                           EqualsInitializedProto(spec),
                           EqualsInitializedProto(run_spec)))
      .WillOnce(Return(Status(::util::error::INTERNAL, "NewNsProcess failed")));
  EXPECT_ERROR_CODE(::util::error::INTERNAL, nscon_->Create(spec, {}));
}

TEST_F(NamespaceControllerCliTest, Create_CustomInit) {
  NamespaceSpec spec;
  spec.mutable_pid();
  spec.mutable_mnt();
  spec.mutable_ipc();
  RunSpec run_spec;

  const vector<string> kArgv = { "string", "of", "argvs" };
  const vector<int> kNamespaces = { CLONE_NEWIPC, CLONE_NEWPID, CLONE_NEWNS };
  SetSupportedNamespaces(kNamespaces);
  map<int, MockNsConfigurator *> mock_configs;
  SetSupportedConfigurators(kNamespaces, &mock_configs, true);
  AddMachineConfigurator(&mock_configs);

  EXPECT_CALL(*mock_pl_,
              NewNsProcess(kArgv, kNamespaces, SizeIs(5),
                           EqualsInitializedProto(spec),
                           EqualsInitializedProto(run_spec)))
      .WillOnce(Return(kPid_));
  EXPECT_CALL(*mock_nsh_factory_, Get(kPid_)).WillOnce(Return(mock_nshandle_));
  EXPECT_CALL(*mock_nshandle_, ToString()).WillOnce(Return(kHandleStr));
  StatusOr<const string> statusor = nscon_->Create(spec, kArgv);
  ASSERT_OK(statusor);
  EXPECT_EQ(kHandleStr, statusor.ValueOrDie());
}

TEST_F(NamespaceControllerCliTest, Create_NoMntnsAndFilesysConfig) {
  // Without MNT namespace, there should be no FilesystemConfigurator.
  NamespaceSpec spec;
  spec.mutable_pid();
  spec.mutable_ipc();
  RunSpec run_spec;

  const string kInitUid = Substitute("--uid=$0", FLAGS_nsinit_uid);
  const string kInitGid = Substitute("--gid=$0", FLAGS_nsinit_gid);
  const vector<string> kArgv = { FLAGS_nsinit_path, kInitUid, kInitGid };
  const vector<int> kNamespaces = { CLONE_NEWIPC, CLONE_NEWPID };
  SetSupportedNamespaces(kNamespaces);
  map<int, MockNsConfigurator *> mock_configs;
  SetSupportedConfigurators(kNamespaces, &mock_configs, false);
  AddMachineConfigurator(&mock_configs);

  EXPECT_CALL(*mock_pl_,
              NewNsProcess(kArgv, kNamespaces, SizeIs(3),
                           EqualsInitializedProto(spec),
                           EqualsInitializedProto(run_spec)))
      .WillOnce(Return(kPid_));
  EXPECT_CALL(*mock_nsh_factory_, Get(kPid_)).WillOnce(Return(mock_nshandle_));
  EXPECT_CALL(*mock_nshandle_, ToString()).WillOnce(Return(kHandleStr));
  StatusOr<const string> statusor = nscon_->Create(spec, {});
  ASSERT_OK(statusor);
  EXPECT_EQ(kHandleStr, statusor.ValueOrDie());
}

TEST_F(NamespaceControllerCliTest, Create_GetFilesystemConfigFailure) {
  NamespaceSpec spec;
  spec.mutable_pid();
  spec.mutable_mnt();
  spec.mutable_ipc();

  // Failed to get the configurator.
  EXPECT_CALL(*mock_config_factory_, GetFilesystemConfigurator())
      .WillOnce(Return(Status(::util::error::INTERNAL,
                              "config_factory failure")));

  EXPECT_ERROR_CODE(::util::error::INTERNAL, nscon_->Create(spec, {}));
}

TEST_F(NamespaceControllerCliTest, Create_FilesystemSpecWithoutMntnsSpec) {
  // Specifying FilesystemSpec without MNTns spec is invalid.
  NamespaceSpec spec;
  spec.mutable_pid();
  spec.mutable_fs();

  EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT, nscon_->Create(spec, {}));
}

TEST_F(NamespaceControllerCliTest, Create_GetMachineConfigFailure) {
  NamespaceSpec spec;
  spec.mutable_pid();
  spec.mutable_mnt();

  // Failed to get the configurator.
  const vector<int> kNamespaces = { CLONE_NEWPID, CLONE_NEWNS };
  SetSupportedNamespaces(kNamespaces);
  map<int, MockNsConfigurator *> mock_configs;
  SetSupportedConfigurators(kNamespaces, &mock_configs, true);
  // Failed to get the configurator.
  EXPECT_CALL(*mock_config_factory_, GetMachineConfigurator())
      .WillOnce(Return(Status(::util::error::INTERNAL,
                              "config_factory failure")));

  EXPECT_ERROR_CODE(::util::error::INTERNAL, nscon_->Create(spec, {}));
}

TEST_F(NamespaceControllerCliTest, RunShellCommand) {
  RunSpec run_spec;
  const vector<int> kNamespaces = {CLONE_NEWPID, CLONE_NEWIPC};
  const pid_t kNewPid = 8080;
  const vector<string> kArgv = { "/bin/bash", "-c", "ls -l" };

  EXPECT_CALL(*mock_nsh_factory_, Get(kHandleStr))
      .WillOnce(Return(mock_nshandle_));
  EXPECT_CALL(*mock_nshandle_, ToPid()).WillOnce(Return(kPid_));
  EXPECT_CALL(*mock_ns_util_, GetUnsharedNamespaces(kPid_))
      .WillOnce(Return(kNamespaces));
  EXPECT_CALL(*mock_pl_, NewNsProcessInTarget(kArgv, kNamespaces, kPid_,
                                              EqualsInitializedProto(run_spec)))
      .WillOnce(Return(kNewPid));
  StatusOr<pid_t> statusor = nscon_->RunShellCommand(kHandleStr, "ls -l",
                                                     run_spec);
  ASSERT_OK(statusor);
  EXPECT_EQ(kNewPid, statusor.ValueOrDie());
}

TEST_F(NamespaceControllerCliTest, Run) {
  RunSpec run_spec;
  const vector<int> kNamespaces = {CLONE_NEWPID, CLONE_NEWIPC};
  const pid_t kNewPid = 8080;
  const vector<string> kArgv = { "/bin/ls", "-l" };

  EXPECT_CALL(*mock_nsh_factory_, Get(kHandleStr))
      .WillOnce(Return(mock_nshandle_));
  EXPECT_CALL(*mock_nshandle_, ToPid()).WillOnce(Return(kPid_));
  EXPECT_CALL(*mock_ns_util_, GetUnsharedNamespaces(kPid_))
      .WillOnce(Return(kNamespaces));
  EXPECT_CALL(*mock_pl_, NewNsProcessInTarget(kArgv, kNamespaces, kPid_,
                                              EqualsInitializedProto(run_spec)))
      .WillOnce(Return(kNewPid));
  StatusOr<pid_t> statusor = nscon_->Run(kHandleStr, kArgv, run_spec);
  ASSERT_OK(statusor);
  EXPECT_EQ(kNewPid, statusor.ValueOrDie());
}

// TODO(adityakali): Add more Run & Exec tests for failure scenarios.

typedef NamespaceControllerCliTest UpdateTest;

TEST_F(UpdateTest, Success) {
  NamespaceSpec spec;
  spec.mutable_pid();
  spec.mutable_mnt();
  spec.mutable_ipc();

  EXPECT_CALL(*mock_nsh_factory_, Get(kHandleStr))
      .WillOnce(Return(mock_nshandle_));
  EXPECT_CALL(*mock_nshandle_, ToPid()).WillOnce(Return(kPid_));
  const vector<int> kNamespaces = { CLONE_NEWPID, CLONE_NEWNS, CLONE_NEWIPC };
  SetSupportedNamespaces(kNamespaces);
  map<int, MockNsConfigurator *> mock_configs;
  SetSupportedConfigurators(kNamespaces, &mock_configs, false);

  for (int ns : kNamespaces) {
    MockSavedNamespace *mock_saved_ns =
        new ::testing::StrictMock<MockSavedNamespace>();
    EXPECT_CALL(*mock_ns_util_, SaveNamespace(ns))
        .WillOnce(Return(mock_saved_ns));
    EXPECT_CALL(*mock_saved_ns, RestoreAndDelete())
        .WillOnce(Invoke([mock_saved_ns]() {
                            delete mock_saved_ns;
                            return Status::OK;
                          }));
  }

  for (int ns : kNamespaces) {
    vector<int> nslist = { ns };
    EXPECT_CALL(*mock_ns_util_, AttachNamespaces(nslist, kPid_))
        .WillOnce(Return(Status::OK));
  }

  ASSERT_OK(nscon_->Update(kHandleStr, spec));
}

TEST_F(UpdateTest, InvalidNshandle) {
  NamespaceSpec spec;
  spec.mutable_pid();
  spec.mutable_mnt();
  spec.mutable_ipc();

  EXPECT_CALL(*mock_nsh_factory_, Get(kHandleStr))
      .WillOnce(Return(Status(INVALID_ARGUMENT, "Invalid nshandle")));

  ASSERT_ERROR_CODE(INVALID_ARGUMENT, nscon_->Update(kHandleStr, spec));
}

TEST_F(UpdateTest, UnsupportedNamespace) {
  NamespaceSpec spec;
  spec.mutable_pid();
  spec.mutable_mnt();
  spec.mutable_ipc();

  EXPECT_CALL(*mock_nsh_factory_, Get(kHandleStr))
      .WillOnce(Return(mock_nshandle_));
  EXPECT_CALL(*mock_nshandle_, ToPid()).WillOnce(Return(kPid_));
  // Assume CLONE_NEWNS is not supported.
  const vector<int> kNamespaces = { CLONE_NEWPID, CLONE_NEWIPC };
  SetSupportedNamespaces(kNamespaces);

  ASSERT_ERROR_CODE(INVALID_ARGUMENT, nscon_->Update(kHandleStr, spec));
}

TEST_F(UpdateTest, UnsupportedConfigurator) {
  NamespaceSpec spec;
  spec.mutable_pid();
  spec.mutable_mnt();
  spec.mutable_ipc();

  EXPECT_CALL(*mock_nsh_factory_, Get(kHandleStr))
      .WillOnce(Return(mock_nshandle_));
  EXPECT_CALL(*mock_nshandle_, ToPid()).WillOnce(Return(kPid_));
  const vector<int> kNamespaces = { CLONE_NEWPID, CLONE_NEWNS, CLONE_NEWIPC };
  SetSupportedNamespaces(kNamespaces);
  map<int, MockNsConfigurator *> mock_configs;
  // Assume mnt doesn't have a configurator.
  SetSupportedConfigurators({CLONE_NEWPID, CLONE_NEWIPC}, &mock_configs, false);

  ASSERT_ERROR_CODE(::util::error::NOT_FOUND, nscon_->Update(kHandleStr, spec));
}

TEST_F(UpdateTest, SetupOutsideNsFailure) {
  NamespaceSpec spec;
  spec.mutable_pid();
  spec.mutable_mnt();
  spec.mutable_ipc();

  EXPECT_CALL(*mock_nsh_factory_, Get(kHandleStr))
      .WillOnce(Return(mock_nshandle_));
  EXPECT_CALL(*mock_nshandle_, ToPid()).WillOnce(Return(kPid_));
  const vector<int> kNamespaces = { CLONE_NEWIPC, CLONE_NEWPID, CLONE_NEWNS };
  SetSupportedNamespaces(kNamespaces);
  map<int, MockNsConfigurator *> mock_configs;
  SetSupportedConfigurators(kNamespaces, &mock_configs, false);

  for (int ns : kNamespaces) {
    MockSavedNamespace *mock_saved_ns =
        new ::testing::StrictMock<MockSavedNamespace>();
    EXPECT_CALL(*mock_ns_util_, SaveNamespace(ns))
        .WillOnce(Return(mock_saved_ns));
    EXPECT_CALL(*mock_saved_ns, RestoreAndDelete())
        .WillRepeatedly(Invoke([mock_saved_ns]() {
                                 delete mock_saved_ns;
                                 return Status::OK;
                               }));
  }

  EXPECT_CALL(*mock_ns_util_, AttachNamespaces(_, kPid_))
      .WillRepeatedly(Return(Status::OK));

  // Assume SetupOutsideNamespace() fails for MNTns.
  EXPECT_CALL(*mock_configs[CLONE_NEWNS],
              SetupOutsideNamespace(EqualsInitializedProto(spec), kPid_))
      .WillOnce(Return(Status(::util::error::INTERNAL, "Setup failed")));

  ASSERT_ERROR_CODE(::util::error::INTERNAL, nscon_->Update(kHandleStr, spec));
}

TEST_F(UpdateTest, AttachNamespacesFailure) {
  NamespaceSpec spec;
  spec.mutable_pid();
  spec.mutable_mnt();
  spec.mutable_ipc();

  EXPECT_CALL(*mock_nsh_factory_, Get(kHandleStr))
      .WillOnce(Return(mock_nshandle_));
  EXPECT_CALL(*mock_nshandle_, ToPid()).WillOnce(Return(kPid_));
  const vector<int> kNamespaces = { CLONE_NEWIPC, CLONE_NEWPID, CLONE_NEWNS };
  SetSupportedNamespaces(kNamespaces);
  map<int, MockNsConfigurator *> mock_configs;
  SetSupportedConfigurators(kNamespaces, &mock_configs, false);

  for (int ns : kNamespaces) {
    MockSavedNamespace *mock_saved_ns =
        new ::testing::StrictMock<MockSavedNamespace>();
    EXPECT_CALL(*mock_ns_util_, SaveNamespace(ns))
        .WillOnce(Return(mock_saved_ns));
    EXPECT_CALL(*mock_saved_ns, RestoreAndDelete())
        .WillRepeatedly(Invoke([mock_saved_ns]() {
                                 delete mock_saved_ns;
                                 return Status::OK;
                               }));
  }

  EXPECT_CALL(*mock_ns_util_, AttachNamespaces(_, kPid_))
      .WillRepeatedly(Return(Status::OK));
  // Assume AttachNamespaces() fails for MNTns.
  const vector<int> nslist = { CLONE_NEWNS };
  EXPECT_CALL(*mock_ns_util_, AttachNamespaces(nslist, kPid_))
      .WillOnce(Return(Status(::util::error::INTERNAL, "AttachNs failed")));

  ASSERT_ERROR_CODE(::util::error::INTERNAL, nscon_->Update(kHandleStr, spec));
}

TEST_F(UpdateTest, SetupInsideNsFailure) {
  NamespaceSpec spec;
  spec.mutable_pid();
  spec.mutable_mnt();
  spec.mutable_ipc();

  EXPECT_CALL(*mock_nsh_factory_, Get(kHandleStr))
      .WillOnce(Return(mock_nshandle_));
  EXPECT_CALL(*mock_nshandle_, ToPid()).WillOnce(Return(kPid_));
  const vector<int> kNamespaces = { CLONE_NEWIPC, CLONE_NEWPID, CLONE_NEWNS };
  SetSupportedNamespaces(kNamespaces);
  map<int, MockNsConfigurator *> mock_configs;
  SetSupportedConfigurators(kNamespaces, &mock_configs, false);

  for (int ns : kNamespaces) {
    MockSavedNamespace *mock_saved_ns =
        new ::testing::StrictMock<MockSavedNamespace>();
    EXPECT_CALL(*mock_ns_util_, SaveNamespace(ns))
        .WillOnce(Return(mock_saved_ns));
    EXPECT_CALL(*mock_saved_ns, RestoreAndDelete())
        .WillRepeatedly(Invoke([mock_saved_ns]() {
                                 delete mock_saved_ns;
                                 return Status::OK;
                               }));
  }

  EXPECT_CALL(*mock_ns_util_, AttachNamespaces(_, kPid_))
      .WillRepeatedly(Return(Status::OK));

  // Assume SetupInsideNamespace() fails for MNTns.
  EXPECT_CALL(*mock_configs[CLONE_NEWNS],
              SetupInsideNamespace(EqualsInitializedProto(spec)))
      .WillOnce(Return(Status(::util::error::INTERNAL, "Setup failed")));

  ASSERT_ERROR_CODE(::util::error::INTERNAL, nscon_->Update(kHandleStr, spec));
}

TEST_F(UpdateTest, SaveNamespaceFailure) {
  NamespaceSpec spec;
  spec.mutable_pid();
  spec.mutable_mnt();
  spec.mutable_ipc();

  EXPECT_CALL(*mock_nsh_factory_, Get(kHandleStr))
      .WillOnce(Return(mock_nshandle_));
  EXPECT_CALL(*mock_nshandle_, ToPid()).WillOnce(Return(kPid_));
  const vector<int> kNamespaces = { CLONE_NEWIPC, CLONE_NEWPID, CLONE_NEWNS };
  SetSupportedNamespaces(kNamespaces);
  map<int, MockNsConfigurator *> mock_configs;
  SetSupportedConfigurators(kNamespaces, &mock_configs, false);

  // Success for IPC and PIDns, but failure for MNTns.
  for (int ns : { CLONE_NEWIPC, CLONE_NEWPID }) {
    MockSavedNamespace *mock_saved_ns =
        new ::testing::StrictMock<MockSavedNamespace>();
    EXPECT_CALL(*mock_ns_util_, SaveNamespace(ns))
        .WillOnce(Return(mock_saved_ns));
    EXPECT_CALL(*mock_saved_ns, RestoreAndDelete())
        .WillRepeatedly(Invoke([mock_saved_ns]() {
                                 delete mock_saved_ns;
                                 return Status::OK;
                               }));
  }
  EXPECT_CALL(*mock_ns_util_, SaveNamespace(CLONE_NEWNS))
        .WillOnce(Return(Status(::util::error::INTERNAL, "Save failed")));

  EXPECT_CALL(*mock_ns_util_, AttachNamespaces(_, kPid_))
      .WillRepeatedly(Return(Status::OK));

  ASSERT_ERROR_CODE(::util::error::INTERNAL, nscon_->Update(kHandleStr, spec));
}

TEST_F(UpdateTest, SavedNamespaceRestoreFailure) {
  NamespaceSpec spec;
  spec.mutable_pid();
  spec.mutable_mnt();
  spec.mutable_ipc();

  EXPECT_CALL(*mock_nsh_factory_, Get(kHandleStr))
      .WillOnce(Return(mock_nshandle_));
  EXPECT_CALL(*mock_nshandle_, ToPid()).WillOnce(Return(kPid_));
  const vector<int> kNamespaces = { CLONE_NEWIPC, CLONE_NEWPID, CLONE_NEWNS };
  SetSupportedNamespaces(kNamespaces);
  map<int, MockNsConfigurator *> mock_configs;
  SetSupportedConfigurators(kNamespaces, &mock_configs, false);

  // Success for IPC and PIDns, but failure for MNTns.
  for (int ns : kNamespaces) {
    MockSavedNamespace *mock_saved_ns =
        new ::testing::StrictMock<MockSavedNamespace>();
    EXPECT_CALL(*mock_ns_util_, SaveNamespace(ns))
        .WillOnce(Return(mock_saved_ns));
    if (ns == CLONE_NEWNS) {
      EXPECT_CALL(*mock_saved_ns, RestoreAndDelete())
          .WillOnce(Return(Status(::util::error::INTERNAL,
                                  "RestoreAndDelete failed")));
    } else {
      EXPECT_CALL(*mock_saved_ns, RestoreAndDelete())
          .WillOnce(Invoke([mock_saved_ns]() {
                             delete mock_saved_ns;
                             return Status::OK;
                           }));
    }
  }

  EXPECT_CALL(*mock_ns_util_, AttachNamespaces(_, kPid_))
      .WillRepeatedly(Return(Status::OK));

  ASSERT_ERROR_CODE(::util::error::INTERNAL, nscon_->Update(kHandleStr, spec));
}

}  // namespace nscon
}  // namespace containers
