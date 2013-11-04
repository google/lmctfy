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

#include "lmctfy/cli/commands/run.h"

#include <sys/types.h>
#include <memory>
#include <vector>

#include "gflags/gflags.h"
#include "lmctfy/cli/output_map.h"
#include "include/lmctfy_mock.h"
#include "util/errors_test_util.h"
#include "strings/substitute.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "util/process/subprocess.h"
#include "util/task/statusor.h"

DECLARE_bool(lmctfy_no_wait);

using ::std::unique_ptr;
using ::std::vector;
using ::strings::Substitute;
using ::testing::ElementsAre;
using ::testing::Ge;
using ::testing::Return;
using ::testing::WhenSorted;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace lmctfy {
namespace cli {
namespace {

static const char kContainerName[] = "/test";
static const char kCmd[] = "echo hi";

class RunTest : public ::testing::Test {
 public:
  RunTest() : argv_({"run", kContainerName, kCmd}) {
  }

  virtual void SetUp() {
    mock_lmctfy_.reset(new StrictMockContainerApi());
    mock_container_ = new StrictMockContainer(kContainerName);
  }

  // Start a no-op child process we can reap. The child will exit with
  // exit_code.
  pid_t StartChild(int exit_code) {
    SubProcess sp;
    sp.SetShellCommand(Substitute("exit $0", exit_code).c_str());
    sp.Start();
    return sp.pid();
  }

 protected:
  const vector<string> argv_;
  unique_ptr<MockContainerApi> mock_lmctfy_;
  MockContainer *mock_container_;
};

TEST_F(RunTest, ForegroundSuccess) {
  vector<OutputMap> output;

  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillRepeatedly(Return(mock_container_));

  EXPECT_CALL(*mock_container_,
              Exec(ElementsAre("/bin/sh", "-c", kCmd)))
      .WillRepeatedly(Return(Status::OK));

  FLAGS_lmctfy_no_wait = false;
  EXPECT_OK(RunInContainer(argv_, mock_lmctfy_.get(), &output));
  EXPECT_EQ(0, output.size());
}

TEST_F(RunTest, ForegroundExecFails) {
  vector<OutputMap> output;

  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillRepeatedly(Return(mock_container_));

  EXPECT_CALL(*mock_container_,
              Exec(ElementsAre("/bin/sh", "-c", kCmd)))
      .WillRepeatedly(Return(Status::CANCELLED));

  FLAGS_lmctfy_no_wait = false;
  EXPECT_EQ(Status::CANCELLED,
            RunInContainer(argv_, mock_lmctfy_.get(), &output));
}

TEST_F(RunTest, ForegroundGetFails) {
  vector<OutputMap> output;

  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillRepeatedly(Return(Status::CANCELLED));

  FLAGS_lmctfy_no_wait = false;
  EXPECT_EQ(Status::CANCELLED,
            RunInContainer(argv_, mock_lmctfy_.get(), &output));

  // The mock container is never returned so delete it.
  delete mock_container_;
}

TEST_F(RunTest, BackgroundSuccess) {
  vector<OutputMap> output;

  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillRepeatedly(Return(mock_container_));

  EXPECT_CALL(*mock_container_, Run(kCmd, Container::FDS_DETACHED))
      .WillRepeatedly(Return(42));

  FLAGS_lmctfy_no_wait = true;
  EXPECT_TRUE(RunInContainer(argv_, mock_lmctfy_.get(), &output).ok());
  ASSERT_EQ(1, output.size());
  EXPECT_EQ("42", output[0].GetValueByKey("pid"));
}

TEST_F(RunTest, BackgroundRunFails) {
  vector<OutputMap> output;

  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillRepeatedly(Return(mock_container_));

  EXPECT_CALL(*mock_container_, Run(kCmd, Container::FDS_DETACHED))
      .WillRepeatedly(Return(Status::CANCELLED));

  FLAGS_lmctfy_no_wait = true;
  EXPECT_EQ(Status::CANCELLED,
            RunInContainer(argv_, mock_lmctfy_.get(), &output));
}

TEST_F(RunTest, BackgroundGetFails) {
  vector<OutputMap> output;

  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillRepeatedly(Return(Status::CANCELLED));

  FLAGS_lmctfy_no_wait = true;
  EXPECT_EQ(Status::CANCELLED,
            RunInContainer(argv_, mock_lmctfy_.get(), &output));

  // The mock container is never returned so delete it.
  delete mock_container_;
}

}  // namespace
}  // namespace cli
}  // namespace lmctfy
}  // namespace containers
