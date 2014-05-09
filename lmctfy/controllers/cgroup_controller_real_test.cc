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

#include "lmctfy/controllers/cgroup_controller.h"

#include <memory>
#include <vector>

#include "gflags/gflags.h"
#include "system_api/kernel_api.h"
#include "file/base/path.h"
#include "lmctfy/controllers/cgroup_factory_mock.h"
#include "lmctfy/controllers/eventfd_notifications_mock.h"
#include "lmctfy/kernel_files.h"
#include "util/errors_test_util.h"
#include "strings/substitute.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "util/task/codes.pb.h"
#include "util/task/status.h"

using ::system_api::KernelAPI;
using ::file::JoinPath;
using ::std::unique_ptr;
using ::std::vector;
using ::strings::Substitute;
using ::testing::Contains;
using ::testing::Return;
using ::testing::StrictMock;
using ::testing::_;
using ::util::Status;
using ::util::StatusOr;
using ::util::error::FAILED_PRECONDITION;
using ::util::error::NOT_FOUND;

DECLARE_string(test_tmpdir);

namespace containers {
namespace lmctfy {

static const CgroupHierarchy kType = CGROUP_MEMORY;
const mode_t kMode = 0755;

class CgroupControllerRealTest : public ::testing::Test {
 protected:
  CgroupControllerRealTest()
      : test_dir_(JoinPath(FLAGS_test_tmpdir, "cgroup_controller_real_test")) {}

  virtual void SetUp() {
    MakeDirectory(test_dir_);
    kernel_.reset(new KernelApi());
    mock_eventfd_notifications_.reset(MockEventFdNotifications::NewStrict());
    controller_.reset(new CgroupController(kType, "", test_dir_, false,
                                           kernel_.get(),
                                           mock_eventfd_notifications_.get()));
  }

  virtual void TearDown() {
    RemoveDirectory(test_dir_);
  }

  void MakeDirectory(const string &path) {
    CHECK_EQ(0, mkdir(path.c_str(), kMode))
        << Substitute("Test failed to mkdir at '$0' with error '$1'",
                      path, StrError(errno));
  }

  void RemoveDirectory(const string &path) {
    CHECK_EQ(0, rmdir(path.c_str()))
        << Substitute("Test failed to rmdir at '$0' with error '$1'",
                      path, StrError(errno));
  }

  void MakeFile(const string &path) {
    CHECK_EQ(0, mknod(path.c_str(), kMode, S_IFREG))
        << Substitute("Test failed to mknod at '$0' with error '$1'",
                      path, StrError(errno));
  }

  void RemoveFile(const string &path) {
    CHECK_EQ(0, unlink(path.c_str()))
        << Substitute("Test failed to unlink at '$0' with error '$1'",
                      path, StrError(errno));
  }

  StatusOr<vector<string>> CallGetSubcontainers() {
    return controller_->GetSubcontainers();
  }

  Status CallDeleteCgroupHierarchy(const string &path) {
    return controller_->DeleteCgroupHierarchy(path);
  }

  Status CallGetSubdirectories(const string &path, vector<string> *entries) {
    return controller_->GetSubdirectories(path, entries);
  }


 protected:
  const string test_dir_;
  unique_ptr<KernelAPI> kernel_;
  unique_ptr<MockEventFdNotifications> mock_eventfd_notifications_;
  unique_ptr<CgroupController> controller_;
};

TEST_F(CgroupControllerRealTest, GetSubdirectories_ManySubdirectories) {
  // Create 3 subcontainers we expect to exist.
  const string kSubdir1 = JoinPath(test_dir_, "sub1");
  const string kSubdir2 = JoinPath(test_dir_, "sub2");
  const string kSubdir3 = JoinPath(test_dir_, "sub3");
  MakeDirectory(kSubdir1);
  MakeDirectory(kSubdir2);
  MakeDirectory(kSubdir3);

  StatusOr<vector<string>> statusor = CallGetSubcontainers();
  ASSERT_OK(statusor);
  EXPECT_EQ(3, statusor.ValueOrDie().size());

  vector<string> subcontainers = statusor.ValueOrDie();
  EXPECT_THAT(subcontainers, Contains("sub1"));
  EXPECT_THAT(subcontainers, Contains("sub2"));
  EXPECT_THAT(subcontainers, Contains("sub3"));

  RemoveDirectory(kSubdir1);
  RemoveDirectory(kSubdir2);
  RemoveDirectory(kSubdir3);
}

TEST_F(CgroupControllerRealTest, GetSubdirectories_NoSubdirectories) {
  StatusOr<vector<string>> statusor = CallGetSubcontainers();
  ASSERT_OK(statusor);
  EXPECT_TRUE(statusor.ValueOrDie().empty());
}

TEST_F(CgroupControllerRealTest, GetSubdirectories_Fails) {
  // Delete the directory so there is nothing to list on.
  RemoveDirectory(test_dir_);

  StatusOr<vector<string>> statusor = CallGetSubcontainers();
  EXPECT_ERROR_CODE(FAILED_PRECONDITION, statusor);

  MakeDirectory(test_dir_);
}


typedef CgroupControllerRealTest DeleteCgroupHierarchyTest;

const char *kDirs[] = { "d1", "d2", "d3", "d4", "d5" };
const char  *kFiles[] = { "f1", "f2", "f3", "f4" };

TEST_F(DeleteCgroupHierarchyTest, SmallerTest) {
  // Create a new base directory.
  string new_base = JoinPath(test_dir_, "base");
  MakeDirectory(new_base);

  // Create directories and files all in the same directory.
  for (auto dir : kDirs) {
    string temp_path = JoinPath(new_base, dir);
    MakeDirectory(temp_path);
  }
  for (auto temp_file : kFiles) {
    string temp_path = JoinPath(new_base, temp_file);
    MakeFile(temp_path);
  }

  // Deletion should fail, but only because the base path has files.
  EXPECT_ERROR_CODE_AND_SUBSTR(FAILED_PRECONDITION, Substitute(
      "Unable to delete directory \"$0\" with error \"Directory not empty\"",
      new_base), CallDeleteCgroupHierarchy(new_base));

  // Expect all the other directories to be deleted already
  vector<string> directories;
  ASSERT_OK(CallGetSubdirectories(new_base, &directories));
  EXPECT_TRUE(directories.empty());

  for (auto temp_file : kFiles) {
    string temp_path = JoinPath(new_base, temp_file);
    RemoveFile(temp_path);
  }

  EXPECT_OK(CallDeleteCgroupHierarchy(new_base));
}

TEST_F(DeleteCgroupHierarchyTest, LargeTest) {
  // Create a new base directory.
  string new_base = JoinPath(test_dir_, "base");
  MakeDirectory(new_base);

  // Create directories and files all in the same directory.
  // Creates two levels of directories, but only one level of files.
  for (auto dir : kDirs) {
    string temp_path = JoinPath(new_base, dir);
    MakeDirectory(temp_path);
    for (auto more_dirs : kDirs) {
      string bigger_temp_path = JoinPath(temp_path, more_dirs);
      MakeDirectory(bigger_temp_path);
    }
  }
  for (auto temp_file : kFiles) {
    string temp_path = JoinPath(new_base, temp_file);
    MakeFile(temp_path);
  }

  // Deletion should fail, but only because the base path has files.
  EXPECT_ERROR_CODE_AND_SUBSTR(FAILED_PRECONDITION, Substitute(
      "Unable to delete directory \"$0\" with error \"Directory not empty\"",
      new_base), CallDeleteCgroupHierarchy(new_base));

  // Expect all the other directories to be deleted already
  vector<string> directories;
  EXPECT_OK(CallGetSubdirectories(new_base, &directories));
  EXPECT_TRUE(directories.empty());

  for (auto temp_file : kFiles) {
    string temp_path = JoinPath(new_base, temp_file);
    RemoveFile(temp_path);
  }

  EXPECT_OK(CallDeleteCgroupHierarchy(new_base));
}


}  // namespace lmctfy
}  // namespace containers
