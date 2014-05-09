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

#include "util/proc_mounts.h"

#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "util/file_lines_test_util.h"

using ::std::string;
using ::std::vector;
using ::testing::Contains;
using ::testing::DoAll;
using ::testing::Invoke;
using ::testing::NotNull;
using ::testing::Return;
using ::testing::StrEq;
using ::testing::StrictMock;
using ::testing::_;

namespace util {
namespace {

static const char kProcMounts[] = "/proc/mounts";

class ProcMountsTest : public ::testing::Test {
 public:
  static void TearDownTestCase() {
    // Gtest doesn't like that this static variable is never deleted.
    delete ::system_api::GlobalLibcFsApi();
  }

  // Expect 3 mounts to be present in the specified mounts_file.
  void ExpectDefaultMounts(const string &mounts_file) {
    mock_file_lines_.ExpectFileLines(
        mounts_file, {"rootfs / rootfs rw 0 0",
                      "sysfs_fs /sys sysfs rw,nosuid,nodev,noexec,relatime 0 0",
                      "proc /proc proc rw,nosuid 2 3",
                      "none /non/existant\\040(deleted) none abc 0 0"});
  }

  // Check the mounts created by ExpectDefaultMounts exist.
  void CheckDefaultMounts(const vector<ProcMountsData> &mounts) {
    ASSERT_EQ(4, mounts.size());

    EXPECT_EQ("rootfs", mounts[0].device);
    EXPECT_EQ("/", mounts[0].mountpoint);
    EXPECT_EQ("rootfs", mounts[0].type);
    ASSERT_EQ(1, mounts[0].options.size());
    EXPECT_THAT(mounts[0].options, Contains("rw"));
    EXPECT_EQ(0, mounts[0].fs_freq);
    EXPECT_EQ(0, mounts[0].fs_passno);

    EXPECT_EQ("sysfs_fs", mounts[1].device);
    EXPECT_EQ("/sys", mounts[1].mountpoint);
    EXPECT_EQ("sysfs", mounts[1].type);
    ASSERT_EQ(5, mounts[1].options.size());
    EXPECT_THAT(mounts[1].options, Contains("rw"));
    EXPECT_THAT(mounts[1].options, Contains("nosuid"));
    EXPECT_THAT(mounts[1].options, Contains("nodev"));
    EXPECT_THAT(mounts[1].options, Contains("noexec"));
    EXPECT_THAT(mounts[1].options, Contains("relatime"));
    EXPECT_EQ(0, mounts[1].fs_freq);
    EXPECT_EQ(0, mounts[1].fs_passno);

    EXPECT_EQ("proc", mounts[2].device);
    EXPECT_EQ("/proc", mounts[2].mountpoint);
    EXPECT_EQ("proc", mounts[2].type);
    ASSERT_EQ(2, mounts[2].options.size());
    EXPECT_THAT(mounts[2].options, Contains("rw"));
    EXPECT_THAT(mounts[2].options, Contains("nosuid"));
    EXPECT_EQ(2, mounts[2].fs_freq);
    EXPECT_EQ(3, mounts[2].fs_passno);

    EXPECT_EQ("none", mounts[3].device);
    EXPECT_EQ("/non/existant", mounts[3].mountpoint);
    EXPECT_EQ("none", mounts[3].type);
    ASSERT_EQ(1, mounts[3].options.size());
    EXPECT_THAT(mounts[3].options, Contains("abc"));
    EXPECT_EQ(0, mounts[3].fs_freq);
    EXPECT_EQ(0, mounts[3].fs_passno);
  }

 protected:
  FileLinesTestUtil mock_file_lines_;
};

TEST_F(ProcMountsTest, AllMounts) {
  ExpectDefaultMounts(kProcMounts);

  vector<ProcMountsData> output_mounts;

  for (const auto &mount : ProcMounts()) {
    output_mounts.push_back(mount);
  }
  CheckDefaultMounts(output_mounts);
}

TEST_F(ProcMountsTest, MountsForPid) {
  ExpectDefaultMounts("/proc/12/mounts");

  vector<ProcMountsData> output_mounts;

  for (const auto &mount : ProcMounts(12)) {
    output_mounts.push_back(mount);
  }
  CheckDefaultMounts(output_mounts);
}

TEST_F(ProcMountsTest, MountsForSelf) {
  ExpectDefaultMounts("/proc/self/mounts");

  vector<ProcMountsData> output_mounts;

  for (const auto &mount : ProcMounts(0)) {
    output_mounts.push_back(mount);
  }
  CheckDefaultMounts(output_mounts);
}

TEST_F(ProcMountsTest, EmptyMounts) {
  mock_file_lines_.ExpectFileLines(kProcMounts, {});

  bool has_mounts = false;
  for (const auto &mount : ProcMounts()) {
    LOG(INFO) << mount.type;
    has_mounts = true;
  }

  EXPECT_FALSE(has_mounts);
}

TEST_F(ProcMountsTest, BadLine) {
  mock_file_lines_.ExpectFileLines(kProcMounts, {"this_line_is_bad"});

  for (const auto &mount : ProcMounts()) {
    EXPECT_EQ("unknown", mount.device);
  }
}

TEST_F(ProcMountsTest, BadFsFreq) {
  mock_file_lines_.ExpectFileLines(kProcMounts, {"rootfs / rootfs rw bad 1"});

  for (const auto &mount : ProcMounts()) {
    EXPECT_EQ(0, mount.fs_freq);
  }
}

TEST_F(ProcMountsTest, BadFsPassno) {
  mock_file_lines_.ExpectFileLines(kProcMounts, {"rootfs / rootfs rw 1 bad"});

  for (const auto &mount : ProcMounts()) {
    EXPECT_EQ(0, mount.fs_passno);
  }
}

}  // namespace
}  // namespace util
