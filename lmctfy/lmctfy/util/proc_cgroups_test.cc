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

#include "lmctfy/util/proc_cgroups.h"

#include <vector>

#include "util/file_lines_test_util.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::std::vector;

namespace containers {
namespace lmctfy {
namespace {

class ProcCgroupsTest : public ::testing::Test {
 protected:
  const string kProcCgroups = "/proc/cgroups";

  ::util::FileLinesTestUtil mock_file_lines_;
};

TEST_F(ProcCgroupsTest, Success) {
  mock_file_lines_.ExpectFileLines(
      kProcCgroups, {"#subsys_name  hierarchy num_cgroups enabled\n",
                     "cpu 8 1 1\n", "memory 12  1 0\n", "freezer  11  2 1\n"});

  vector<ProcCgroupsData> data;
  for (const auto &cgroup : ProcCgroups()) {
    data.push_back(cgroup);
  }

  ASSERT_EQ(3, data.size());

  // CPU
  EXPECT_EQ("cpu", data[0].hierarchy_name);
  EXPECT_EQ(8, data[0].hierarchy_id);
  EXPECT_EQ(1, data[0].num_cgroups);
  EXPECT_TRUE(data[0].enabled);

  // Memory
  EXPECT_EQ("memory", data[1].hierarchy_name);
  EXPECT_EQ(12, data[1].hierarchy_id);
  EXPECT_EQ(1, data[1].num_cgroups);
  EXPECT_FALSE(data[1].enabled);

  // Freezer
  EXPECT_EQ("freezer", data[2].hierarchy_name);
  EXPECT_EQ(11, data[2].hierarchy_id);
  EXPECT_EQ(2, data[2].num_cgroups);
  EXPECT_TRUE(data[2].enabled);
}

TEST_F(ProcCgroupsTest, BadNumberOfElements) {
  mock_file_lines_.ExpectFileLines(kProcCgroups, {"cpu 1 1 1 1\n"});

  bool has_cgroups = false;
  for (const auto &cgroup : ProcCgroups()) {
    LOG(INFO) << cgroup.hierarchy_id;
    has_cgroups = true;
  }

  EXPECT_FALSE(has_cgroups);
}

TEST_F(ProcCgroupsTest, BadHierarchyId) {
  mock_file_lines_.ExpectFileLines(kProcCgroups, {"cpu bad 1 1\n"});

  bool has_cgroups = false;
  for (const auto &cgroup : ProcCgroups()) {
    LOG(INFO) << cgroup.hierarchy_id;
    has_cgroups = true;
  }

  EXPECT_FALSE(has_cgroups);
}

TEST_F(ProcCgroupsTest, BadNumCgroups) {
  mock_file_lines_.ExpectFileLines(kProcCgroups, {"cpu 1 bad 1\n"});

  bool has_cgroups = false;
  for (const auto &cgroup : ProcCgroups()) {
    LOG(INFO) << cgroup.num_cgroups;
    has_cgroups = true;
  }

  EXPECT_FALSE(has_cgroups);
}

TEST_F(ProcCgroupsTest, BadEnabled) {
  mock_file_lines_.ExpectFileLines(kProcCgroups, {"cpu 1 1 bad\n"});

  bool has_cgroups = false;
  for (const auto &cgroup : ProcCgroups()) {
    LOG(INFO) << cgroup.enabled;
    has_cgroups = true;
  }

  EXPECT_FALSE(has_cgroups);
}

}  // namespace
}  // namespace lmctfy
}  // namespace containers
