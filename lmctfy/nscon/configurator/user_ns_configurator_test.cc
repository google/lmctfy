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
// Tests for UserNsConfigurator class
//
#include "nscon/configurator/user_ns_configurator.h"

#include <fcntl.h>

#include <memory>

#include "include/namespaces.pb.h"
#include "nscon/ns_util_mock.h"
#include "util/errors_test_util.h"
#include "system_api/libc_fs_api_test_util.h"
#include "gtest/gtest.h"
#include "util/task/status.h"

using ::std::unique_ptr;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::StrEq;
using ::testing::_;
using ::util::error::INTERNAL;
using ::util::error::INVALID_ARGUMENT;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace nscon {

class UserNsConfiguratorTest : public ::testing::Test {
 public:
  void SetUp() override {
    mock_ns_util_.reset(new ::testing::StrictMock<MockNsUtil>());
    user_ns_config_.reset(new UserNsConfigurator(mock_ns_util_.get()));
  }

  Status CallSetupUserNamespace(const UserNsSpec &user_spec,
                                pid_t init_pid) const {
    return user_ns_config_->SetupUserNamespace(user_spec, init_pid);
  }

  Status CallWriteIdMap(const string &id_map_file,
                        const vector<IdMapEntry> &id_map) const {
    return user_ns_config_->WriteIdMap(id_map_file, id_map);
  }

  StatusOr<vector<IdMapEntry>> CallValidateIdMap(
      const ::google::protobuf::RepeatedPtrField<IdMapEntry> &id_map) const {
    return user_ns_config_->ValidateIdMap(id_map);
  }

 protected:
  struct IdMapEntryData {
   public:
    IdMapEntryData(int in, int out, int len)
        : id_in(in), id_out(out), length(len) {}
    int id_in;
    int id_out;
    int length;
  };

  vector<IdMapEntry> GetIdMapEntryVector(
      const vector<IdMapEntryData> &entry_list) {
    vector<IdMapEntry> id_list;
    for (IdMapEntryData data : entry_list) {
      IdMapEntry *entry = test_userns_.add_uid_map();
      SetIdMapEntry(entry, data.id_in, data.id_out, data.length);
      id_list.push_back(*entry);
    }
    return id_list;
  }

  // The value '0' (zero) is special for all these inputs. If they are zero,
  // then corresponding field in IdMapEntry is left unset.
  void SetIdMapEntry(IdMapEntry *entry, int id_in, int id_out, int length) {
    if (id_in != 0) entry->set_id_inside_ns(id_in);
    if (id_out != 0) entry->set_id_outside_ns(id_out);
    if (length != 0) entry->set_length(length);
  }

  void AddUidMapEntry(UserNsSpec *userns, int id_in, int id_out, int length) {
    IdMapEntry *entry = userns->add_uid_map();
    SetIdMapEntry(entry, id_in, id_out, length);
  }

  void AddGidMapEntry(UserNsSpec *userns, int id_in, int id_out, int length) {
    IdMapEntry *entry = userns->add_gid_map();
    SetIdMapEntry(entry, id_in, id_out, length);
  }

  system_api::MockLibcFsApiOverride mock_libc_fs_api_;
  unique_ptr<MockNsUtil> mock_ns_util_;
  unique_ptr<UserNsConfigurator> user_ns_config_;
  UserNsSpec test_userns_;
  const char *kUidMapFile_ = "/proc/9999/uid_map";
  const char *kGidMapFile_ = "/proc/9999/gid_map";
  const int kUidMapFd_ = 55;
  const int kGidMapFd_ = 66;
  const pid_t kPid_ = 9999;
};

TEST_F(UserNsConfiguratorTest, SetupInsideNamespace_NoSpec) {
  NamespaceSpec spec;
  ASSERT_OK(user_ns_config_->SetupInsideNamespace(spec));
}

TEST_F(UserNsConfiguratorTest, SetupInsideNamespace_WithSpec) {
  // Even with any userns spec, SetupInsideNamespace() should return OK without
  // actually doing anything.
  NamespaceSpec spec;
  UserNsSpec *userns = spec.mutable_user();
  AddUidMapEntry(userns, 99, 99, 1);
  AddGidMapEntry(userns, 99, 99, 1);
  ASSERT_OK(user_ns_config_->SetupInsideNamespace(spec));
}

TEST_F(UserNsConfiguratorTest, SetupOutsideNamespace_NoSpec) {
  NamespaceSpec spec;
  ASSERT_OK(user_ns_config_->SetupOutsideNamespace(spec, kPid_));
}

TEST_F(UserNsConfiguratorTest, SetupOutsideNamespace_EmptyUserSpec) {
  NamespaceSpec spec;
  spec.mutable_user();
  ASSERT_OK(user_ns_config_->SetupOutsideNamespace(spec, kPid_));
}

typedef UserNsConfiguratorTest WriteIdMapTest;

const char *g_write_data;
static int WriteVerifier(int fd, const void *buf, size_t size) {
//  const char *str = static_cast<const char *>(buf);
  StringPiece sp(static_cast<const char *>(buf), size);
  EXPECT_EQ(g_write_data, sp.ToString());
  EXPECT_EQ(strlen(g_write_data), size);
  return sizeof(g_write_data);
}

TEST_F(WriteIdMapTest, Success) {
  g_write_data = "99 99 1\n5000 5000 1\n";
  const vector<IdMapEntry> id_map = GetIdMapEntryVector({
    {99, 99, 1},
    {5000, 5000, 1},
  });

  EXPECT_CALL(mock_libc_fs_api_.Mock(), Open(StrEq(kUidMapFile_), O_WRONLY))
      .WillOnce(Return(kUidMapFd_));
  EXPECT_CALL(mock_libc_fs_api_.Mock(), Write(kUidMapFd_, _, _))
      .WillOnce(Invoke(&WriteVerifier));
  EXPECT_CALL(mock_libc_fs_api_.Mock(), Close(kUidMapFd_)).WillOnce(Return(0));

  EXPECT_OK(CallWriteIdMap("/proc/9999/uid_map", id_map));
}

TEST_F(WriteIdMapTest, EmptyIdMap) {
  const vector<IdMapEntry> id_map;
  EXPECT_OK(CallWriteIdMap(kUidMapFile_, id_map));
}

TEST_F(WriteIdMapTest, OpenFailure) {
  const vector<IdMapEntry> id_map = GetIdMapEntryVector({
    {99, 99, 1},
    {5000, 5000, 1},
  });

  EXPECT_CALL(mock_libc_fs_api_.Mock(), Open(StrEq(kUidMapFile_), O_WRONLY))
      .WillOnce(Return(-1));

  EXPECT_ERROR_CODE(INTERNAL, CallWriteIdMap(kUidMapFile_, id_map));
}

TEST_F(WriteIdMapTest, WriteFailure) {
  g_write_data = "99 99 1\n5000 5000 1\n";
  const vector<IdMapEntry> id_map = GetIdMapEntryVector({
    {99, 99, 1},
    {5000, 5000, 1},
  });

  EXPECT_CALL(mock_libc_fs_api_.Mock(), Open(StrEq(kUidMapFile_), O_WRONLY))
      .WillOnce(Return(kUidMapFd_));
  EXPECT_CALL(mock_libc_fs_api_.Mock(), Write(kUidMapFd_, _, _))
      .WillOnce(Return(-1));
  EXPECT_CALL(mock_libc_fs_api_.Mock(), Close(kUidMapFd_)).WillOnce(Return(0));

  EXPECT_ERROR_CODE(INTERNAL, CallWriteIdMap(kUidMapFile_, id_map));
}

TEST_F(WriteIdMapTest, CloseFailure) {
  g_write_data = "99 99 1\n5000 5000 1\n";
  const vector<IdMapEntry> id_map = GetIdMapEntryVector({
    {99, 99, 1},
    {5000, 5000, 1},
  });

  EXPECT_CALL(mock_libc_fs_api_.Mock(), Open(StrEq(kUidMapFile_), O_WRONLY))
      .WillOnce(Return(kUidMapFd_));
  EXPECT_CALL(mock_libc_fs_api_.Mock(), Write(kUidMapFd_, _, _))
      .WillOnce(Invoke(&WriteVerifier));
  EXPECT_CALL(mock_libc_fs_api_.Mock(), Close(kUidMapFd_)).WillOnce(Return(-1));

  EXPECT_ERROR_CODE(INTERNAL, CallWriteIdMap(kUidMapFile_, id_map));
}

typedef UserNsConfiguratorTest ValidateIdMapTest;

TEST_F(ValidateIdMapTest, Success) {
  NamespaceSpec spec;
  UserNsSpec *userns = spec.mutable_user();
  AddUidMapEntry(userns, 99, 99, 1);
  AddUidMapEntry(userns, 5000, 5000, 1);
  AddUidMapEntry(userns, 8000, 8000, 1000);

  StatusOr<vector<IdMapEntry>> statusor = CallValidateIdMap(userns->uid_map());
  ASSERT_OK(statusor);
  EXPECT_EQ(3, statusor.ValueOrDie().size());
}

TEST_F(ValidateIdMapTest, SuccessWithEmptyMap) {
  NamespaceSpec spec;
  UserNsSpec *userns = spec.mutable_user();

  StatusOr<vector<IdMapEntry>> statusor = CallValidateIdMap(userns->uid_map());
  ASSERT_OK(statusor);
  EXPECT_EQ(0, statusor.ValueOrDie().size());
}

TEST_F(ValidateIdMapTest, NoIdInside) {
  NamespaceSpec spec;
  UserNsSpec *userns = spec.mutable_user();
  AddUidMapEntry(userns, 99, 99, 1);
  AddUidMapEntry(userns, 0, 5000, 1);  // '0' implies the value wont be set.
  AddUidMapEntry(userns, 8000, 8000, 1000);

  EXPECT_ERROR_CODE(INVALID_ARGUMENT, CallValidateIdMap(userns->uid_map()));
}

TEST_F(ValidateIdMapTest, NoIdOutside) {
  NamespaceSpec spec;
  UserNsSpec *userns = spec.mutable_user();
  AddUidMapEntry(userns, 99, 99, 1);
  AddUidMapEntry(userns, 5000, 0, 1);  // '0' implies the value wont be set.
  AddUidMapEntry(userns, 8000, 8000, 1000);

  EXPECT_ERROR_CODE(INVALID_ARGUMENT, CallValidateIdMap(userns->uid_map()));
}

TEST_F(ValidateIdMapTest, NoLength) {
  NamespaceSpec spec;
  UserNsSpec *userns = spec.mutable_user();
  AddUidMapEntry(userns, 99, 99, 1);
  AddUidMapEntry(userns, 5000, 5000, 0);  // '0' implies the value wont be set.
  AddUidMapEntry(userns, 8000, 8000, 1000);

  EXPECT_ERROR_CODE(INVALID_ARGUMENT, CallValidateIdMap(userns->uid_map()));
}

TEST_F(ValidateIdMapTest, MultipleMissingFields) {
  NamespaceSpec spec;
  UserNsSpec *userns = spec.mutable_user();
  AddUidMapEntry(userns, 99, 99, 1);
  AddUidMapEntry(userns, 0, 5000, 0);  // '0' implies the value wont be set.
  AddUidMapEntry(userns, 8000, 8000, 1000);

  EXPECT_ERROR_CODE(INVALID_ARGUMENT, CallValidateIdMap(userns->uid_map()));
}

typedef UserNsConfiguratorTest SetupUserNamespaceTest;

TEST_F(SetupUserNamespaceTest, EmptySpec) {
  UserNsSpec userns;
  ASSERT_OK(CallSetupUserNamespace(userns, kPid_));
}

TEST_F(SetupUserNamespaceTest, ValidSpec) {
  UserNsSpec userns;
  AddUidMapEntry(&userns, 99, 99, 1);
  AddUidMapEntry(&userns, 5000, 5000, 1);
  AddUidMapEntry(&userns, 8000, 8000, 1000);
  AddGidMapEntry(&userns, 99, 99, 1);
  AddGidMapEntry(&userns, 5000, 5000, 1);
  AddGidMapEntry(&userns, 8000, 8000, 1000);

  g_write_data = "99 99 1\n5000 5000 1\n8000 8000 1000\n";

  // Expect uid_map file write.
  EXPECT_CALL(mock_libc_fs_api_.Mock(), Open(StrEq(kUidMapFile_), O_WRONLY))
      .WillOnce(Return(kUidMapFd_));
  EXPECT_CALL(mock_libc_fs_api_.Mock(), Write(kUidMapFd_, _, _))
      .WillOnce(Invoke(&WriteVerifier));
  EXPECT_CALL(mock_libc_fs_api_.Mock(), Close(kUidMapFd_)).WillOnce(Return(0));

  // Expect gid_map file write.
  EXPECT_CALL(mock_libc_fs_api_.Mock(), Open(StrEq(kGidMapFile_), O_WRONLY))
      .WillOnce(Return(kGidMapFd_));
  EXPECT_CALL(mock_libc_fs_api_.Mock(), Write(kGidMapFd_, _, _))
      .WillOnce(Invoke(&WriteVerifier));
  EXPECT_CALL(mock_libc_fs_api_.Mock(), Close(kGidMapFd_)).WillOnce(Return(0));

  ASSERT_OK(CallSetupUserNamespace(userns, kPid_));
}

TEST_F(SetupUserNamespaceTest, GidMapWriteFailure) {
  UserNsSpec userns;
  AddUidMapEntry(&userns, 99, 99, 1);
  AddUidMapEntry(&userns, 5000, 5000, 1);
  AddUidMapEntry(&userns, 8000, 8000, 1000);
  AddGidMapEntry(&userns, 99, 99, 1);
  AddGidMapEntry(&userns, 5000, 5000, 1);
  AddGidMapEntry(&userns, 8000, 8000, 1000);

  g_write_data = "99 99 1\n5000 5000 1\n8000 8000 1000\n";

  // Expect uid_map file write.
  EXPECT_CALL(mock_libc_fs_api_.Mock(), Open(StrEq(kUidMapFile_), O_WRONLY))
      .WillOnce(Return(kUidMapFd_));
  EXPECT_CALL(mock_libc_fs_api_.Mock(), Write(kUidMapFd_, _, _))
      .WillOnce(Invoke(&WriteVerifier));
  EXPECT_CALL(mock_libc_fs_api_.Mock(), Close(kUidMapFd_)).WillOnce(Return(0));

  // Expect gid_map file write.
  EXPECT_CALL(mock_libc_fs_api_.Mock(), Open(StrEq(kGidMapFile_), O_WRONLY))
      .WillOnce(Return(kGidMapFd_));
  EXPECT_CALL(mock_libc_fs_api_.Mock(), Write(kGidMapFd_, _, _))
      .WillOnce(Invoke(&WriteVerifier));
  // Close() fails for gid_map.
  EXPECT_CALL(mock_libc_fs_api_.Mock(), Close(kGidMapFd_)).WillOnce(Return(-1));

  EXPECT_ERROR_CODE(INTERNAL, CallSetupUserNamespace(userns, kPid_));
}

TEST_F(SetupUserNamespaceTest, UidMapWriteFailure) {
  // If uid_map write fails, we don't even try to write the gid_map.
  UserNsSpec userns;
  AddUidMapEntry(&userns, 99, 99, 1);
  AddUidMapEntry(&userns, 5000, 5000, 1);
  AddUidMapEntry(&userns, 8000, 8000, 1000);
  AddGidMapEntry(&userns, 99, 99, 1);
  AddGidMapEntry(&userns, 5000, 5000, 1);
  AddGidMapEntry(&userns, 8000, 8000, 1000);

  g_write_data = "99 99 1\n5000 5000 1\n8000 8000 1000\n";

  // Expect uid_map file write.
  EXPECT_CALL(mock_libc_fs_api_.Mock(), Open(StrEq(kUidMapFile_), O_WRONLY))
      .WillOnce(Return(kUidMapFd_));
  EXPECT_CALL(mock_libc_fs_api_.Mock(), Write(kUidMapFd_, _, _))
      .WillOnce(Invoke(&WriteVerifier));
  // Close() fails for uid_map.
  EXPECT_CALL(mock_libc_fs_api_.Mock(), Close(kUidMapFd_)).WillOnce(Return(-1));

  EXPECT_ERROR_CODE(INTERNAL, CallSetupUserNamespace(userns, kPid_));
}

}  // namespace nscon
}  // namespace containers
