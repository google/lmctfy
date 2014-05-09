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

#include "lmctfy/util/proc_cgroup.h"

#include "util/file_lines_test_util.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::std::vector;
using ::testing::Contains;
using ::testing::ElementsAre;

namespace containers {
namespace lmctfy {
namespace {

static const char kProcSelfCgroup[] = "/proc/self/cgroup";

class ProcCgroupTest : public ::testing::Test {
 public:
  void ExpectDefaultCgroups(const string &file) {
    mock_file_lines_.ExpectFileLines(
        file, {"1:cpuacct,cpu:/sys", "2:cpuset:/", "3:io:/sys"});
  }

  void CheckDefaultCgroups(const vector<ProcCgroupData> &cgroups) {
    ASSERT_EQ(3, cgroups.size());

    EXPECT_EQ(1, cgroups[0].hierarchy_id);
    ASSERT_EQ(2, cgroups[0].subsystems.size());
    EXPECT_THAT(cgroups[0].subsystems, Contains("cpu"));
    EXPECT_THAT(cgroups[0].subsystems, Contains("cpuacct"));
    EXPECT_EQ("/sys", cgroups[0].hierarchy_path);

    EXPECT_EQ(2, cgroups[1].hierarchy_id);
    ASSERT_EQ(1, cgroups[1].subsystems.size());
    EXPECT_THAT(cgroups[1].subsystems, Contains("cpuset"));
    EXPECT_EQ("/", cgroups[1].hierarchy_path);

    EXPECT_EQ(3, cgroups[2].hierarchy_id);
    ASSERT_EQ(1, cgroups[2].subsystems.size());
    EXPECT_THAT(cgroups[2].subsystems, Contains("io"));
    EXPECT_EQ("/sys", cgroups[2].hierarchy_path);
  }

 protected:
  ::util::FileLinesTestUtil mock_file_lines_;
};

TEST_F(ProcCgroupTest, SelfCgroups) {
  ExpectDefaultCgroups(kProcSelfCgroup);

  vector<ProcCgroupData> output;
  for (const auto &cgroup : ProcCgroup(0)) {
    output.push_back(cgroup);
  }
  CheckDefaultCgroups(output);
}

TEST_F(ProcCgroupTest, PidCgroups) {
  ExpectDefaultCgroups("/proc/10/cgroup");

  vector<ProcCgroupData> output;
  for (const auto &cgroup : ProcCgroup(10)) {
    output.push_back(cgroup);
  }
  CheckDefaultCgroups(output);
}

TEST_F(ProcCgroupTest, EmptyCgroups) {
  mock_file_lines_.ExpectFileLines(kProcSelfCgroup, {});

  bool has_cgroups = false;
  for (const auto &cgroup : ProcCgroup(0)) {
    LOG(INFO) << cgroup.hierarchy_id;
    has_cgroups = true;
  }

  EXPECT_FALSE(has_cgroups);
}

TEST_F(ProcCgroupTest, BadLine) {
  mock_file_lines_.ExpectFileLines(kProcSelfCgroup, {"this_line_is_bad"});

  bool has_cgroups = false;
  for (const auto &cgroup : ProcCgroup(0)) {
    LOG(INFO) << cgroup.hierarchy_id;
    has_cgroups = true;
  }

  EXPECT_FALSE(has_cgroups);
}

TEST_F(ProcCgroupTest, BadHierarchyId) {
  mock_file_lines_.ExpectFileLines(kProcSelfCgroup, {"potato:cpu:/"});

  bool has_cgroups = false;
  for (const auto &cgroup : ProcCgroup(0)) {
    LOG(INFO) << cgroup.hierarchy_id;
    has_cgroups = true;
  }

  EXPECT_FALSE(has_cgroups);
}

}  // namespace
}  // namespace lmctfy
}  // namespace containers
