// Copyright 2013 Google Inc. All Rights Reserved.
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

#include "lmctfy/controllers/cgroup_controller.h"

#include <memory>
#include <vector>

#include "gflags/gflags.h"
#include "system_api/kernel_api_mock.h"
#include "file/base/path.h"
#include "lmctfy/controllers/cgroup_factory_mock.h"
#include "lmctfy/controllers/eventfd_notifications_mock.h"
#include "lmctfy/kernel_files.h"
#include "util/safe_types/unix_gid.h"
#include "util/safe_types/unix_uid.h"
#include "util/errors_test_util.h"
#include "util/file_lines_test_util.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "util/task/codes.pb.h"
#include "util/task/status.h"

using ::system_api::KernelAPIMock;
using ::file::JoinPath;
using ::util::FileLines;
using ::util::FileLinesTestUtil;
using ::util::UnixGid;
using ::util::UnixGidValue;
using ::util::UnixUid;
using ::util::UnixUidValue;
using ::std::unique_ptr;
using ::std::vector;
using ::testing::ContainerEq;
using ::testing::Contains;
using ::testing::DoAll;
using ::testing::NotNull;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::StrictMock;
using ::testing::_;
using ::util::Status;
using ::util::StatusOr;
using ::util::error::NOT_FOUND;

DECLARE_string(test_tmpdir);

namespace containers {
namespace lmctfy {

// Passthrough MemoryController for testing.
class TestMemoryController : public CgroupController {
 public:
  TestMemoryController(const string &cgroup_path, bool owns_cgroup,
                       const KernelApi *kernel,
                       EventFdNotifications *eventfd_notifications)
      : CgroupController(CGROUP_MEMORY, cgroup_path, owns_cgroup, kernel,
                         eventfd_notifications) {}
  virtual ~TestMemoryController() {}
};

// Passthrough MemoryControllerFactory for testing.
class TestMemoryControllerFactory
    : public CgroupControllerFactory<TestMemoryController, CGROUP_MEMORY> {
 public:
  TestMemoryControllerFactory(const CgroupFactory *cgroup_factory,
                              const KernelApi *kernel,
                              EventFdNotifications *eventfd_notifications)
      : CgroupControllerFactory<TestMemoryController, CGROUP_MEMORY>(
            cgroup_factory, kernel, eventfd_notifications) {}
  virtual ~TestMemoryControllerFactory() {}
};

static const CgroupHierarchy kType = CGROUP_MEMORY;
static const char kContainerName[] = "/test";
static const char kCgroupPath[] = "/dev/cgroup/memory/test";
static const char kCgroupTasksPath[] = "/dev/cgroup/memory/test/tasks";
static const char kCgroupClonePath[] =
    "/dev/cgroup/memory/test/cgroup.clone_children";
static const char kCgroupProcsPath[] = "/dev/cgroup/memory/test/cgroup.procs";
static const char kMemoryLimit[] = "memory.limit_in_bytes";
static const char kCgroupMemoryLimitPath[] =
    "/dev/cgroup/memory/test/memory.limit_in_bytes";

class CgroupControllerFactoryTest : public ::testing::Test {
 public:
  CgroupControllerFactoryTest()
      : kBools({true, false}),
        root_uid_(UnixUidValue::Root()),
        root_gid_(UnixGidValue::Root()) {}

  virtual void SetUp() {
    mock_kernel_.reset(new StrictMock<KernelAPIMock>());
    mock_eventfd_notifications_.reset(MockEventFdNotifications::NewStrict());
    mock_cgroup_factory_.reset(new StrictMockCgroupFactory());
  }

  // Setup a new factory instance. It will own the underlying cgroup if that is
  // specified.
  void SetupFactory(bool owns_cgroup) {
    EXPECT_CALL(*mock_cgroup_factory_, OwnsCgroup(kType))
        .WillRepeatedly(Return(owns_cgroup));

    factory_.reset(new TestMemoryControllerFactory(
        mock_cgroup_factory_.get(), mock_kernel_.get(),
        mock_eventfd_notifications_.get()));
  }

 protected:
  const vector<bool> kBools;
  const UnixUid root_uid_;
  const UnixGid root_gid_;

  unique_ptr<MockEventFdNotifications> mock_eventfd_notifications_;
  unique_ptr<KernelAPIMock> mock_kernel_;
  unique_ptr<MockCgroupFactory> mock_cgroup_factory_;
  unique_ptr<TestMemoryControllerFactory> factory_;
};

TEST_F(CgroupControllerFactoryTest, GetSuccess) {
  for (bool owns_cgroup : kBools) {
    SetupFactory(owns_cgroup);

    EXPECT_CALL(*mock_cgroup_factory_, Get(kType, kContainerName))
        .WillRepeatedly(Return(string(kCgroupPath)));

    StatusOr<TestMemoryController *> statusor = factory_->Get(kContainerName);
    ASSERT_TRUE(statusor.ok());
    EXPECT_NE(nullptr, statusor.ValueOrDie());
    unique_ptr<CgroupController> controller(statusor.ValueOrDie());
    EXPECT_EQ(kType, controller->type());
    EXPECT_EQ(owns_cgroup, controller->owns_cgroup());
  }
}

TEST_F(CgroupControllerFactoryTest, GetCgroupFactoryGetFails) {
  for (bool owns_cgroup : kBools) {
    SetupFactory(owns_cgroup);

    EXPECT_CALL(*mock_cgroup_factory_, Get(kType, kContainerName))
        .WillRepeatedly(Return(Status::CANCELLED));

    EXPECT_EQ(Status::CANCELLED, factory_->Get(kContainerName).status());
  }
}

TEST_F(CgroupControllerFactoryTest, CreateSuccess) {
  for (bool owns_cgroup : kBools) {
    SetupFactory(owns_cgroup);

    EXPECT_CALL(*mock_cgroup_factory_,
                Create(kType, kContainerName, root_uid_, root_gid_))
        .WillRepeatedly(Return(string(kCgroupPath)));

    StatusOr<TestMemoryController *> statusor =
        factory_->Create(kContainerName, root_uid_, root_gid_);
    ASSERT_TRUE(statusor.ok());
    EXPECT_NE(nullptr, statusor.ValueOrDie());
    unique_ptr<CgroupController> controller(statusor.ValueOrDie());
    EXPECT_EQ(kType, controller->type());
    EXPECT_EQ(owns_cgroup, controller->owns_cgroup());
  }
}

TEST_F(CgroupControllerFactoryTest, CreateCgroupFactoryCreateFails) {
  for (bool owns_cgroup : kBools) {
    SetupFactory(owns_cgroup);

    EXPECT_CALL(*mock_cgroup_factory_,
                Create(kType, kContainerName, root_uid_, root_gid_))
        .WillRepeatedly(Return(Status::CANCELLED));

    EXPECT_EQ(Status::CANCELLED,
              factory_->Create(kContainerName, root_uid_, root_gid_).status());
  }
}

TEST_F(CgroupControllerFactoryTest, ExistsSuccess) {
  for (bool owns_cgroup : kBools) {
    SetupFactory(owns_cgroup);

    EXPECT_CALL(*mock_cgroup_factory_, Get(kType, kContainerName))
        .WillRepeatedly(Return(string(kCgroupPath)));

    EXPECT_TRUE(factory_->Exists(kContainerName));
  }
}

TEST_F(CgroupControllerFactoryTest, ExistsDoesNotExist) {
  for (bool owns_cgroup : kBools) {
    SetupFactory(owns_cgroup);

    EXPECT_CALL(*mock_cgroup_factory_, Get(kType, kContainerName))
        .WillRepeatedly(Return(Status(NOT_FOUND, "Not found")));

    EXPECT_FALSE(factory_->Exists(kContainerName));
  }
}

TEST_F(CgroupControllerFactoryTest, HierarchyNameSuccess) {
  const string kHierarchyName = "cgroup_hierarchy";

  for (bool owns_cgroup : kBools) {
    SetupFactory(owns_cgroup);

    EXPECT_CALL(*mock_cgroup_factory_, GetHierarchyName(kType))
        .WillOnce(Return(kHierarchyName));

    StatusOr<string> statusor = factory_->HierarchyName();
    ASSERT_OK(statusor);
    EXPECT_EQ(kHierarchyName, statusor.ValueOrDie());
  }
}

class CgroupControllerTest : public ::testing::Test {
 public:
  virtual void SetUp() {
    mock_kernel_.reset(new StrictMock<KernelAPIMock>());
    mock_eventfd_notifications_.reset(MockEventFdNotifications::NewStrict());
    controller_.reset(NewCgroupController(kType, kCgroupPath, true,
                                          mock_kernel_.get(),
                                          mock_eventfd_notifications_.get()));
  }

  void ReSetUpWithUnownedResource() {
    controller_.reset(NewCgroupController(kType, kCgroupPath, false,
                                          mock_kernel_.get(),
                                          mock_eventfd_notifications_.get()));
  }

  // Create a new CgroupController.
  CgroupController *NewCgroupController(
      CgroupHierarchy type, const string &cgroup_path, bool owns_cgroup,
      const KernelApi *kernel, EventFdNotifications *eventfd_notifications) {
    return new CgroupController(type, cgroup_path, owns_cgroup, kernel,
                                eventfd_notifications);
  }

  // Wrappers for protected methods.

  Status CallSetParamBool(const string &hierarchy_file, int64 value) {
    return controller_->SetParamBool(hierarchy_file, value);
  }

  Status CallSetParamInt(const string &hierarchy_file, int64 value) {
    return controller_->SetParamInt(hierarchy_file, value);
  }

  Status CallSetParamString(const string &hierarchy_file, const string &value) {
    return controller_->SetParamString(hierarchy_file, value);
  }

  StatusOr<string> CallGetParamString(const string &hierarchy_file) const {
    return controller_->GetParamString(hierarchy_file);
  }

  StatusOr<bool> CallGetParamBool(const string &hierarchy_file) const {
    return controller_->GetParamBool(hierarchy_file);
  }

  StatusOr<int64> CallGetParamInt(const string &hierarchy_file) const {
    return controller_->GetParamInt(hierarchy_file);
  }

  StatusOr<FileLines> CallGetParamLines(const string &hierarchy_file) const {
    return controller_->GetParamLines(hierarchy_file);
  }

  StatusOr<ActiveNotifications::Handle> CallRegisterNotification(
      const string &cgroup_file, const string &arguments,
      CgroupController::EventCallback *callback) {
    return controller_->RegisterNotification(cgroup_file, arguments, callback);
  }

 protected:
  FileLinesTestUtil mock_file_lines_;
  unique_ptr<MockEventFdNotifications> mock_eventfd_notifications_;
  unique_ptr<KernelAPIMock> mock_kernel_;
  unique_ptr<CgroupController> controller_;
};

TEST_F(CgroupControllerTest, DestroySuccess) {
  EXPECT_CALL(*mock_kernel_, RmDir(kCgroupPath)).WillRepeatedly(Return(0));

  EXPECT_TRUE(controller_.release()->Destroy().ok());
}

TEST_F(CgroupControllerTest, DestroySuccessDoesNotOwnCgroup) {
  unique_ptr<CgroupController> controller(
      NewCgroupController(kType, kCgroupPath, false, mock_kernel_.get(),
                          mock_eventfd_notifications_.get()));

  // Does not rmdir.
  EXPECT_TRUE(controller.release()->Destroy().ok());
}

TEST_F(CgroupControllerTest, DestroyRmDirFails) {
  EXPECT_CALL(*mock_kernel_, RmDir(kCgroupPath)).WillRepeatedly(Return(-1));

  EXPECT_EQ(::util::error::FAILED_PRECONDITION,
            controller_->Destroy().error_code());
}

TEST_F(CgroupControllerTest, EnterSuccess) {
  EXPECT_CALL(*mock_kernel_, SafeWriteResFileWithRetry(
                                 3, "42", kCgroupTasksPath, NotNull(),
                                 NotNull())).WillRepeatedly(Return(true));

  EXPECT_TRUE(controller_->Enter(42).ok());
}

TEST_F(CgroupControllerTest, EnterOpenError) {
  EXPECT_CALL(*mock_kernel_,
              SafeWriteResFileWithRetry(3, "42", kCgroupTasksPath, NotNull(),
                                        NotNull()))
      .WillRepeatedly(DoAll(SetArgPointee<3>(true), Return(true)));

  Status status = controller_->Enter(42);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(NOT_FOUND, status.error_code());
}

TEST_F(CgroupControllerTest, EnterWriteError) {
  EXPECT_CALL(*mock_kernel_,
              SafeWriteResFileWithRetry(3, "42", kCgroupTasksPath, NotNull(),
                                        NotNull()))
      .WillRepeatedly(DoAll(SetArgPointee<4>(true), Return(true)));

  Status status = controller_->Enter(42);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(::util::error::UNAVAILABLE, status.error_code());
}

TEST_F(CgroupControllerTest, GetThreadsSuccess) {
  const vector<pid_t> kPids = {1, 2, 3, 4};
  const string kPidStrings = "1\n2\n3\n4\n";
  EXPECT_CALL(*mock_kernel_, Access(kCgroupTasksPath, F_OK))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kCgroupTasksPath, NotNull()))
      .WillRepeatedly(DoAll(SetArgPointee<1>(kPidStrings), Return(true)));

  StatusOr<vector<pid_t>> statusor = controller_->GetThreads();
  ASSERT_TRUE(statusor.ok());
  EXPECT_THAT(statusor.ValueOrDie(), ContainerEq(kPids));
}

TEST_F(CgroupControllerTest, GetThreadsEmpty) {
  EXPECT_CALL(*mock_kernel_, Access(kCgroupTasksPath, F_OK))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kCgroupTasksPath, NotNull()))
      .WillRepeatedly(DoAll(SetArgPointee<1>(""), Return(true)));

  StatusOr<vector<pid_t>> statusor = controller_->GetThreads();
  ASSERT_TRUE(statusor.ok());
  EXPECT_TRUE(statusor.ValueOrDie().empty());
}

TEST_F(CgroupControllerTest, GetThreadsFails) {
  EXPECT_CALL(*mock_kernel_, Access(kCgroupTasksPath, F_OK))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kCgroupTasksPath, NotNull()))
      .WillRepeatedly(Return(false));

  EXPECT_FALSE(controller_->GetThreads().ok());
}

TEST_F(CgroupControllerTest, GetProcessesSuccess) {
  const vector<pid_t> kPids = {1, 2, 3, 4};
  const string kPidStrings = "1\n2\n3\n4\n";
  EXPECT_CALL(*mock_kernel_, Access(kCgroupProcsPath, F_OK))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kCgroupProcsPath, NotNull()))
      .WillRepeatedly(DoAll(SetArgPointee<1>(kPidStrings), Return(true)));

  StatusOr<vector<pid_t>> statusor = controller_->GetProcesses();
  ASSERT_TRUE(statusor.ok());
  EXPECT_THAT(statusor.ValueOrDie(), ContainerEq(kPids));
}

TEST_F(CgroupControllerTest, GetProcessesEmpty) {
  EXPECT_CALL(*mock_kernel_, Access(kCgroupProcsPath, F_OK))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kCgroupProcsPath, NotNull()))
      .WillRepeatedly(DoAll(SetArgPointee<1>(""), Return(true)));

  StatusOr<vector<pid_t>> statusor = controller_->GetProcesses();
  ASSERT_TRUE(statusor.ok());
  EXPECT_TRUE(statusor.ValueOrDie().empty());
}

TEST_F(CgroupControllerTest, GetProcessesFails) {
  EXPECT_CALL(*mock_kernel_, Access(kCgroupProcsPath, F_OK))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kCgroupProcsPath, NotNull()))
      .WillRepeatedly(Return(false));

  EXPECT_FALSE(controller_->GetProcesses().ok());
}

TEST_F(CgroupControllerTest, SetParamStringSuccess) {
  EXPECT_CALL(*mock_kernel_, SafeWriteResFileWithRetry(
                                 3, "42", kCgroupTasksPath, NotNull(),
                                 NotNull())).WillRepeatedly(Return(true));

  EXPECT_TRUE(CallSetParamString("tasks", "42").ok());
}

TEST_F(CgroupControllerTest, SetParamStringOpenError) {
  EXPECT_CALL(*mock_kernel_,
              SafeWriteResFileWithRetry(3, "42", kCgroupTasksPath, NotNull(),
                                        NotNull()))
      .WillRepeatedly(DoAll(SetArgPointee<3>(true), Return(true)));

  Status status = CallSetParamString("tasks", "42");
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(NOT_FOUND, status.error_code());
}

TEST_F(CgroupControllerTest, SetParamStringWriteError) {
  EXPECT_CALL(*mock_kernel_,
              SafeWriteResFileWithRetry(3, "42", kCgroupTasksPath, NotNull(),
                                        NotNull()))
      .WillRepeatedly(DoAll(SetArgPointee<4>(true), Return(true)));

  Status status = CallSetParamString("tasks", "42");
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(::util::error::UNAVAILABLE, status.error_code());
}

TEST_F(CgroupControllerTest, SetParamIntSuccess) {
  EXPECT_CALL(*mock_kernel_, SafeWriteResFileWithRetry(
                                 3, "42", kCgroupTasksPath, NotNull(),
                                 NotNull())).WillRepeatedly(Return(true));

  EXPECT_TRUE(CallSetParamInt("tasks", 42).ok());
}

TEST_F(CgroupControllerTest, SetParamIntOpenError) {
  EXPECT_CALL(*mock_kernel_,
              SafeWriteResFileWithRetry(3, "42", kCgroupTasksPath, NotNull(),
                                        NotNull()))
      .WillRepeatedly(DoAll(SetArgPointee<3>(true), Return(true)));

  Status status = CallSetParamInt("tasks", 42);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(NOT_FOUND, status.error_code());
}

TEST_F(CgroupControllerTest, SetParamIntWriteError) {
  EXPECT_CALL(*mock_kernel_,
              SafeWriteResFileWithRetry(3, "42", kCgroupTasksPath, NotNull(),
                                        NotNull()))
      .WillRepeatedly(DoAll(SetArgPointee<4>(true), Return(true)));

  Status status = CallSetParamInt("tasks", 42);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(::util::error::UNAVAILABLE, status.error_code());
}

TEST_F(CgroupControllerTest, SetParamBoolSuccess) {
  // True.
  EXPECT_CALL(*mock_kernel_, SafeWriteResFileWithRetry(3, "1", kCgroupTasksPath,
                                                       NotNull(), NotNull()))
      .WillRepeatedly(Return(true));

  EXPECT_OK(CallSetParamBool("tasks", true));

  // False.
  EXPECT_CALL(*mock_kernel_, SafeWriteResFileWithRetry(3, "0", kCgroupTasksPath,
                                                       NotNull(), NotNull()))
      .WillRepeatedly(Return(true));

  EXPECT_OK(CallSetParamBool("tasks", false));
}

TEST_F(CgroupControllerTest, SetParamBoolOpenError) {
  EXPECT_CALL(*mock_kernel_, SafeWriteResFileWithRetry(3, "1", kCgroupTasksPath,
                                                       NotNull(), NotNull()))
      .WillRepeatedly(DoAll(SetArgPointee<3>(true), Return(true)));

  EXPECT_ERROR_CODE(NOT_FOUND, CallSetParamBool("tasks", true));
}

TEST_F(CgroupControllerTest, SetParamBoolWriteError) {
  EXPECT_CALL(*mock_kernel_, SafeWriteResFileWithRetry(3, "1", kCgroupTasksPath,
                                                       NotNull(), NotNull()))
      .WillRepeatedly(DoAll(SetArgPointee<4>(true), Return(true)));

  EXPECT_ERROR_CODE(::util::error::UNAVAILABLE,
                    CallSetParamBool("tasks", true));
}

TEST_F(CgroupControllerTest, GetParamStringSuccess) {
  const string kContents = "file_contents";

  EXPECT_CALL(*mock_kernel_, Access(kCgroupMemoryLimitPath, F_OK))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_,
              ReadFileToString(kCgroupMemoryLimitPath, NotNull()))
      .WillRepeatedly(DoAll(SetArgPointee<1>(kContents), Return(true)));

  StatusOr<string> statusor = CallGetParamString(kMemoryLimit);
  ASSERT_TRUE(statusor.ok());
  EXPECT_EQ(kContents, statusor.ValueOrDie());
}

TEST_F(CgroupControllerTest, GetParamStringFileDoesNotExist) {
  EXPECT_CALL(*mock_kernel_, Access(kCgroupMemoryLimitPath, F_OK))
      .WillRepeatedly(Return(1));

  EXPECT_ERROR_CODE(NOT_FOUND, CallGetParamString(kMemoryLimit));
}

TEST_F(CgroupControllerTest, GetParamStringReadFails) {
  EXPECT_CALL(*mock_kernel_, Access(kCgroupMemoryLimitPath, F_OK))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_,
              ReadFileToString(kCgroupMemoryLimitPath, NotNull()))
      .WillRepeatedly(Return(false));

  StatusOr<string> statusor = CallGetParamString(kMemoryLimit);
  EXPECT_FALSE(statusor.ok());
  EXPECT_EQ(::util::error::FAILED_PRECONDITION, statusor.status().error_code());
}

TEST_F(CgroupControllerTest, GetParamBoolSuccess) {
  const string kContents = "1";

  EXPECT_CALL(*mock_kernel_, Access(kCgroupMemoryLimitPath, F_OK))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_,
              ReadFileToString(kCgroupMemoryLimitPath, NotNull()))
      .WillRepeatedly(DoAll(SetArgPointee<1>(kContents), Return(true)));

  StatusOr<bool> statusor = CallGetParamBool(kMemoryLimit);
  ASSERT_OK(statusor);
  EXPECT_EQ(true, statusor.ValueOrDie());
}

TEST_F(CgroupControllerTest, GetParamBoolFileDoesNotExist) {
  EXPECT_CALL(*mock_kernel_, Access(kCgroupMemoryLimitPath, F_OK))
      .WillRepeatedly(Return(1));

  EXPECT_ERROR_CODE(NOT_FOUND, CallGetParamBool(kMemoryLimit));
}

TEST_F(CgroupControllerTest, GetParamBoolReadFails) {
  EXPECT_CALL(*mock_kernel_, Access(kCgroupMemoryLimitPath, F_OK))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_,
              ReadFileToString(kCgroupMemoryLimitPath, NotNull()))
      .WillRepeatedly(Return(false));

  EXPECT_ERROR_CODE(::util::error::FAILED_PRECONDITION,
                    CallGetParamBool(kMemoryLimit).status());
}

TEST_F(CgroupControllerTest, GetParamBoolConvertToIntFails) {
  const string kContents = "not_an_int";

  EXPECT_CALL(*mock_kernel_, Access(kCgroupMemoryLimitPath, F_OK))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_,
              ReadFileToString(kCgroupMemoryLimitPath, NotNull()))
      .WillRepeatedly(DoAll(SetArgPointee<1>(kContents), Return(true)));

  EXPECT_ERROR_CODE(::util::error::FAILED_PRECONDITION,
                    CallGetParamBool(kMemoryLimit).status());
}

TEST_F(CgroupControllerTest, GetParamBoolInvalidBoolValue) {
  const string kContents = "42";

  EXPECT_CALL(*mock_kernel_, Access(kCgroupMemoryLimitPath, F_OK))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_,
              ReadFileToString(kCgroupMemoryLimitPath, NotNull()))
      .WillRepeatedly(DoAll(SetArgPointee<1>(kContents), Return(true)));

  EXPECT_ERROR_CODE(::util::error::OUT_OF_RANGE,
                    CallGetParamBool(kMemoryLimit).status());
}

TEST_F(CgroupControllerTest, GetParamIntSuccess) {
  const string kContents = "42";

  EXPECT_CALL(*mock_kernel_, Access(kCgroupMemoryLimitPath, F_OK))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_,
              ReadFileToString(kCgroupMemoryLimitPath, NotNull()))
      .WillRepeatedly(DoAll(SetArgPointee<1>(kContents), Return(true)));

  StatusOr<int64> statusor = CallGetParamInt(kMemoryLimit);
  ASSERT_TRUE(statusor.ok());
  EXPECT_EQ(42, statusor.ValueOrDie());
}

TEST_F(CgroupControllerTest, GetParamIntFileDoesNotExist) {
  EXPECT_CALL(*mock_kernel_, Access(kCgroupMemoryLimitPath, F_OK))
      .WillRepeatedly(Return(1));

  EXPECT_ERROR_CODE(NOT_FOUND, CallGetParamInt(kMemoryLimit));
}

TEST_F(CgroupControllerTest, GetParamIntReadFails) {
  EXPECT_CALL(*mock_kernel_, Access(kCgroupMemoryLimitPath, F_OK))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_,
              ReadFileToString(kCgroupMemoryLimitPath, NotNull()))
      .WillRepeatedly(Return(false));

  StatusOr<int64> statusor = CallGetParamInt(kMemoryLimit);
  EXPECT_FALSE(statusor.ok());
  EXPECT_EQ(::util::error::FAILED_PRECONDITION, statusor.status().error_code());
}

TEST_F(CgroupControllerTest, GetParamIntConvertToIntFails) {
  const string kContents = "not_an_int";

  EXPECT_CALL(*mock_kernel_, Access(kCgroupMemoryLimitPath, F_OK))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_,
              ReadFileToString(kCgroupMemoryLimitPath, NotNull()))
      .WillRepeatedly(DoAll(SetArgPointee<1>(kContents), Return(true)));

  StatusOr<int64> statusor = CallGetParamInt(kMemoryLimit);
  EXPECT_FALSE(statusor.ok());
  EXPECT_EQ(::util::error::FAILED_PRECONDITION, statusor.status().error_code());
}

TEST_F(CgroupControllerTest, GetParamLinesSuccess) {
  EXPECT_CALL(*mock_kernel_, Access(kCgroupMemoryLimitPath, F_OK))
      .WillRepeatedly(Return(0));
  mock_file_lines_.ExpectFileLines(kCgroupMemoryLimitPath, {"1", "2", "3"});

  StatusOr<FileLines> statusor = CallGetParamLines(kMemoryLimit);
  ASSERT_OK(statusor);
  vector<string> lines;
  for (const StringPiece &line : statusor.ValueOrDie()) {
    lines.push_back(line.ToString());
  }
  ASSERT_EQ(3, lines.size());
  EXPECT_EQ("1", lines[0]);
  EXPECT_EQ("2", lines[1]);
  EXPECT_EQ("3", lines[2]);
}

TEST_F(CgroupControllerTest, GetParamLinesEmptyFile) {
  EXPECT_CALL(*mock_kernel_, Access(kCgroupMemoryLimitPath, F_OK))
      .WillRepeatedly(Return(0));
  mock_file_lines_.ExpectFileLines(kCgroupMemoryLimitPath, {});

  StatusOr<FileLines> statusor = CallGetParamLines(kMemoryLimit);
  ASSERT_OK(statusor);
  bool has_lines = false;
  for (const StringPiece &line : statusor.ValueOrDie()) {
    LOG(INFO) << line.ToString();
    has_lines = true;
  }
  ASSERT_FALSE(has_lines);
}

TEST_F(CgroupControllerTest, GetParamLinesFileDoesNotExist) {
  EXPECT_CALL(*mock_kernel_, Access(kCgroupMemoryLimitPath, F_OK))
      .WillRepeatedly(Return(1));

  EXPECT_ERROR_CODE(NOT_FOUND, CallGetParamLines(kMemoryLimit));
}

// Tests for EnableCloneChildren().

TEST_F(CgroupControllerTest, EnableCloneChildrenSuccess) {
  EXPECT_CALL(*mock_kernel_, SafeWriteResFileWithRetry(3, "1", kCgroupClonePath,
                                                       NotNull(), NotNull()))
      .WillRepeatedly(Return(true));

  EXPECT_OK(controller_->EnableCloneChildren());
}

TEST_F(CgroupControllerTest, EnableCloneChildrenFailure) {
  EXPECT_CALL(*mock_kernel_, SafeWriteResFileWithRetry(3, "1", kCgroupClonePath,
                                                       NotNull(), NotNull()))
      .WillRepeatedly(DoAll(SetArgPointee<3>(true), Return(true)));

  EXPECT_ERROR_CODE(NOT_FOUND, controller_->EnableCloneChildren());
}

// Tests for DisableCloneChildren().

TEST_F(CgroupControllerTest, DisableCloneChildrenSuccess) {
  EXPECT_CALL(*mock_kernel_, SafeWriteResFileWithRetry(3, "0", kCgroupClonePath,
                                                       NotNull(), NotNull()))
      .WillRepeatedly(Return(true));

  EXPECT_OK(controller_->DisableCloneChildren());
}

TEST_F(CgroupControllerTest, DisableCloneChildrenFailure) {
  EXPECT_CALL(*mock_kernel_, SafeWriteResFileWithRetry(3, "0", kCgroupClonePath,
                                                       NotNull(), NotNull()))
      .WillRepeatedly(DoAll(SetArgPointee<3>(true), Return(true)));

  EXPECT_ERROR_CODE(NOT_FOUND, controller_->DisableCloneChildren());
}

// Tests for RegisterNotification().

// Dummy callback that should never be called.
void EventCallback(Status status) { CHECK(false) << "Should never be called"; }

TEST_F(CgroupControllerTest, RegisterNotificationSuccess) {
  const string kCgroupFile = "memory.oom_control";
  const string kArg = "1024";

  EXPECT_CALL(*mock_eventfd_notifications_,
              RegisterNotification(kCgroupPath, kCgroupFile, kArg, NotNull()))
      .WillOnce(Return(1));

  unique_ptr<EventFdNotifications::EventCallback> cb(
      NewPermanentCallback(&EventCallback));
  EXPECT_OK(CallRegisterNotification(kCgroupFile, kArg, cb.get()));
}

TEST_F(CgroupControllerTest, RegisterNotificationFails) {
  const string kCgroupFile = "memory.oom_control";
  const string kArg = "1024";

  EXPECT_CALL(*mock_eventfd_notifications_,
              RegisterNotification(kCgroupPath, kCgroupFile, kArg, NotNull()))
      .WillOnce(Return(Status::CANCELLED));

  unique_ptr<EventFdNotifications::EventCallback> cb(
      NewPermanentCallback(&EventCallback));
  EXPECT_ERROR_CODE(::util::error::CANCELLED,
                    CallRegisterNotification(kCgroupFile, kArg, cb.get()));
}

TEST_F(CgroupControllerTest, RegisterNotificationBadCallback) {
  EXPECT_DEATH(CallRegisterNotification("", "", nullptr).status().IgnoreError(),
               "Must be non NULL");
  unique_ptr<EventFdNotifications::EventCallback> cb(
      NewCallback(&EventCallback));
  EXPECT_DEATH(
      CallRegisterNotification("", "", cb.get()).status().IgnoreError(),
      "not a repeatable callback");
}

TEST_F(CgroupControllerTest, SetLimit) {
  const string kResFile =
      JoinPath(kCgroupPath, KernelFiles::CGroup::Children::kLimit);

  EXPECT_CALL(*mock_kernel_, SafeWriteResFileWithRetry(_, "42", kResFile,
                                                       NotNull(), NotNull()))
      .WillOnce(Return(0));

  EXPECT_OK(controller_->SetChildrenLimit(42));
}

TEST_F(CgroupControllerTest, SetChildrenLimitFails) {
  const string kResFile =
      JoinPath(kCgroupPath, KernelFiles::CGroup::Children::kLimit);

  EXPECT_CALL(*mock_kernel_, SafeWriteResFileWithRetry(_, "42", kResFile,
                                                       NotNull(), NotNull()))
      .WillOnce(DoAll(SetArgPointee<4>(true), Return(0)));

  EXPECT_NOT_OK(controller_->SetChildrenLimit(42));
}

TEST_F(CgroupControllerTest, UnownedSetChildrenLimitIgnored) {
  ReSetUpWithUnownedResource();
  const string kResFile =
      JoinPath(kCgroupPath, KernelFiles::CGroup::Children::kLimit);

  // If unowned, don't set the children limit.
  EXPECT_CALL(*mock_kernel_, SafeWriteResFileWithRetry(_, "42", kResFile,
                                                       NotNull(), NotNull()))
      .Times(0);

  EXPECT_OK(controller_->SetChildrenLimit(42));
}


TEST_F(CgroupControllerTest, GetChildrenLimit) {
  const string kResFile =
      JoinPath(kCgroupPath, KernelFiles::CGroup::Children::kLimit);

  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kResFile, NotNull()))
      .WillOnce(DoAll(SetArgPointee<1>("42"), Return(true)));

  StatusOr<int64> statusor = controller_->GetChildrenLimit();
  ASSERT_OK(statusor);
  EXPECT_EQ(42, statusor.ValueOrDie());
}

TEST_F(CgroupControllerTest, GetChildrenLimitNotFound) {
  const string kResFile =
      JoinPath(kCgroupPath, KernelFiles::CGroup::Children::kLimit);

  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK))
      .WillRepeatedly(Return(1));

  EXPECT_ERROR_CODE(NOT_FOUND, controller_->GetChildrenLimit());
}

TEST_F(CgroupControllerTest, GetChildrenLimitFails) {
  const string kResFile =
      JoinPath(kCgroupPath, KernelFiles::CGroup::Children::kLimit);

  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kResFile, NotNull()))
      .WillOnce(Return(false));

  EXPECT_NOT_OK(controller_->GetChildrenLimit());
}

class GetSubdirectoriesTest : public ::testing::Test {
 public:
  GetSubdirectoriesTest()
      : basepath_(JoinPath(FLAGS_test_tmpdir, "subdirs_test")) {
    // Create and ensure the existence of the test directory.
    mkdir(basepath_.c_str(), 0777);
    struct stat ignored;
    CHECK_EQ(0, stat(basepath_.c_str(), &ignored));
  }

  virtual void SetUp() {
    mock_kernel_.reset(new StrictMock<KernelAPIMock>());
    mock_eventfd_notifications_.reset(MockEventFdNotifications::NewStrict());
    controller_.reset(NewCgroupController(kType, basepath_, false,
                                          mock_kernel_.get(),
                                          mock_eventfd_notifications_.get()));
  }

  // Create a new CgroupController.
  CgroupController *NewCgroupController(
      CgroupHierarchy type, const string &cgroup_path, bool owns_cgroup,
      const KernelApi *kernel, EventFdNotifications *eventfd_notifications) {
    return new CgroupController(type, cgroup_path, owns_cgroup, kernel,
                                eventfd_notifications);
  }

  StatusOr<vector<string>> CallGetSubdirectories() {
    return controller_->GetSubdirectories();
  }

 protected:
  const string basepath_;

  unique_ptr<KernelAPIMock> mock_kernel_;
  unique_ptr<MockEventFdNotifications> mock_eventfd_notifications_;
  unique_ptr<CgroupController> controller_;
};

TEST_F(GetSubdirectoriesTest, ManySubdirectories) {
  // Create 3 subcontainers we expect to exist.
  const string kSubdir1 = JoinPath(basepath_, "sub1");
  const string kSubdir2 = JoinPath(basepath_, "sub2");
  const string kSubdir3 = JoinPath(basepath_, "sub3");
  ASSERT_EQ(0, mkdir(kSubdir1.c_str(), 0777));
  ASSERT_EQ(0, mkdir(kSubdir2.c_str(), 0777));
  ASSERT_EQ(0, mkdir(kSubdir3.c_str(), 0777));

  StatusOr<vector<string>> statusor = CallGetSubdirectories();
  ASSERT_TRUE(statusor.ok());
  EXPECT_EQ(3, statusor.ValueOrDie().size());

  vector<string> subcontainers = statusor.ValueOrDie();
  EXPECT_THAT(subcontainers, Contains("sub1"));
  EXPECT_THAT(subcontainers, Contains("sub2"));
  EXPECT_THAT(subcontainers, Contains("sub3"));

  rmdir(kSubdir1.c_str());
  rmdir(kSubdir2.c_str());
  rmdir(kSubdir3.c_str());
}

TEST_F(GetSubdirectoriesTest, NoSubdirectories) {
  StatusOr<vector<string>> statusor = CallGetSubdirectories();
  ASSERT_TRUE(statusor.ok());
  EXPECT_TRUE(statusor.ValueOrDie().empty());
}

TEST_F(GetSubdirectoriesTest, Fails) {
  // Delete the directory so there is nothing to list on.
  rmdir(basepath_.c_str());

  StatusOr<vector<string>> statusor = CallGetSubdirectories();
  EXPECT_FALSE(statusor.ok());
  EXPECT_EQ(::util::error::FAILED_PRECONDITION, statusor.status().error_code());

  ASSERT_EQ(0, mkdir(basepath_.c_str(), 0777));
}

}  // namespace lmctfy
}  // namespace containers
