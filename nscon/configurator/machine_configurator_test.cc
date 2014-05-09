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
// Tests for MachineConfigurator class
//
#include "nscon/configurator/machine_configurator.h"

#include <sys/stat.h>
#include <sys/types.h>

#include <memory>

#include "file/base/file.h"
#include "file/base/helpers.h"
#include "file/base/path.h"
#include "google/protobuf/text_format.h"
#include "nscon/ns_util_mock.h"
#include "include/namespaces.pb.h"
#include "global_utils/fs_utils_test_util.h"
#include "global_utils/mount_utils_test_util.h"
#include "util/errors_test_util.h"
#include "gtest/gtest.h"
#include "util/task/status.h"
#include "util/task/statusor.h"

DECLARE_string(test_tmpdir);

using ::util::MountUtils;
using ::std::unique_ptr;
#include "util/testing/equals_initialized_proto.h"
using ::testing::EqualsInitializedProto;
using ::testing::IsEmpty;
using ::testing::Return;
using ::testing::StrEq;
using ::testing::_;
using ::util::error::INTERNAL;
using ::util::error::NOT_FOUND;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace nscon {

class MachineConfiguratorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    mock_ns_util_.reset(new ::testing::StrictMock<MockNsUtil>());
    machine_config_.reset(new MachineConfigurator(mock_ns_util_.get()));
    machine_spec_dir_ = file::JoinPath(FLAGS_test_tmpdir, "test_machine_spec");
    machine_spec_file_ = ".machine.spec";
  }

  Status CallSetupRunTmpfs() const {
    return machine_config_->SetupRunTmpfs();
  }

  Status CallWriteMachineSpec(const MachineSpec &spec) const {
    return machine_config_->WriteMachineSpec(spec, machine_spec_dir_,
                                             machine_spec_file_);
  }

  Status ReadMachineSpecFromFile(MachineSpec *spec) {
    string file_output;
    RETURN_IF_ERROR(file::GetContents(file::JoinPath(machine_spec_dir_,
                                                     machine_spec_file_),
                                      &file_output, file::Defaults()));
    if (!google::protobuf::TextFormat::ParseFromString(file_output, spec)) {
      return Status(INTERNAL, "ParseFromString failed.");
    }

    return Status::OK;
  }

 protected:
  unique_ptr<MockNsUtil> mock_ns_util_;
  unique_ptr<MachineConfigurator> machine_config_;
  util::MockFsUtilsOverride mock_fs_utils_;
  util::MockMountUtilsOverride mock_mount_utils_;
  string machine_spec_dir_;
  string machine_spec_file_;
};

TEST_F(MachineConfiguratorTest, SetupOutsideNamespace_Success) {
  NamespaceSpec spec;
  EXPECT_OK(machine_config_->SetupOutsideNamespace(spec, 1));
}

TEST_F(MachineConfiguratorTest, SetupInsideNamespace_NoMachineSpec) {
  NamespaceSpec spec;
  EXPECT_OK(machine_config_->SetupInsideNamespace(spec));
  spec.mutable_fs();
  EXPECT_OK(machine_config_->SetupInsideNamespace(spec));
}

typedef MachineConfiguratorTest SetupRunTmpfsTest;

TEST_F(SetupRunTmpfsTest, NoMountNeeded) {
  MountUtils::MountObject obj;
  EXPECT_CALL(mock_mount_utils_.Mock(), GetMountInfo(StrEq("/run")))
      .WillRepeatedly(Return(StatusOr<MountUtils::MountObject>(obj)));
  ASSERT_OK(CallSetupRunTmpfs());
}

TEST_F(SetupRunTmpfsTest, NoRunDir_Success) {
  EXPECT_CALL(mock_mount_utils_.Mock(), GetMountInfo(StrEq("/run")))
      .WillOnce(Return(StatusOr<MountUtils::MountObject>(
          Status(NOT_FOUND, "Dir doesn't exist, probably"))));
  EXPECT_CALL(mock_fs_utils_.Mock(), SafeEnsureDir(StrEq("/run"), 0777))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(mock_mount_utils_.Mock(), MountTmpfs(StrEq("/run"), _, IsEmpty()))
      .WillOnce(Return(Status::OK));
  ASSERT_OK(CallSetupRunTmpfs());
}

TEST_F(SetupRunTmpfsTest, EnsureDirError) {
  EXPECT_CALL(mock_mount_utils_.Mock(), GetMountInfo(StrEq("/run")))
      .WillOnce(Return(StatusOr<MountUtils::MountObject>(
          Status(NOT_FOUND, "Dir mnt doesn't exist, probably"))));
  EXPECT_CALL(mock_fs_utils_.Mock(), SafeEnsureDir(StrEq("/run"), 0777))
      .WillOnce(Return(Status(INTERNAL, "Failed to Ensure dir...")));
  EXPECT_ERROR_CODE(INTERNAL, CallSetupRunTmpfs());
}


TEST_F(SetupRunTmpfsTest, MountError) {
  EXPECT_CALL(mock_mount_utils_.Mock(), GetMountInfo(StrEq("/run")))
      .WillOnce(Return(StatusOr<MountUtils::MountObject>(
          Status(NOT_FOUND, "Dir mnt doesn't exist, probably"))));
  EXPECT_CALL(mock_fs_utils_.Mock(), SafeEnsureDir(StrEq("/run"), 0777))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(mock_mount_utils_.Mock(), MountTmpfs(StrEq("/run"), _, IsEmpty()))
      .WillOnce(Return(Status(INTERNAL, "Failed to mount...")));
  EXPECT_ERROR_CODE(INTERNAL, CallSetupRunTmpfs());
}

typedef MachineConfiguratorTest WriteMachineSpecTest;

TEST_F(WriteMachineSpecTest, Success) {
  MachineSpec spec;
  const string kName = "/test";
  auto *virt_root1 = spec.mutable_virtual_root()->add_cgroup_virtual_root();
  virt_root1->set_root(kName);
  virt_root1->set_hierarchy(CGROUP_CPU);
  auto *virt_root2 = spec.mutable_virtual_root()->add_cgroup_virtual_root();
  virt_root2->set_root(kName);
  virt_root2->set_hierarchy(CGROUP_MEMORY);

  // Create the directory.
  mkdir(machine_spec_dir_.c_str(), 0777);
  EXPECT_CALL(mock_fs_utils_.Mock(), SafeEnsureDir(StrEq(machine_spec_dir_),
                                                   0777))
      .WillOnce(Return(Status::OK));
  ASSERT_OK(CallWriteMachineSpec(spec));

  string file_data;
  MachineSpec written_spec;
  ASSERT_OK(ReadMachineSpecFromFile(&written_spec));
  EXPECT_THAT(spec, EqualsInitializedProto(written_spec));
}

TEST_F(WriteMachineSpecTest, FailedToCreateDir) {
  MachineSpec spec;
  EXPECT_CALL(mock_fs_utils_.Mock(), SafeEnsureDir(StrEq(machine_spec_dir_),
                                                   0777))
      .WillOnce(Return(Status(INTERNAL, "Failed to create the dir...")));
  EXPECT_ERROR_CODE(INTERNAL, CallWriteMachineSpec(spec));
}

}  // namespace nscon
}  // namespace containers
