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
#include "util/testing/equals_initialized_proto.h"
using ::testing::EqualsInitializedProto;
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
static const char *kArgv[] = {"echo", "hi"};

class RunExecTest : public ::testing::Test {
 public:
  RunExecTest() : argv_({"run", kContainerName, kArgv[0], kArgv[1]}) {
    detached_spec_.set_fd_policy(RunSpec::DETACHED);
  }

  virtual void SetUp() {
    mock_lmctfy_.reset(new StrictMockContainerApi());
    mock_container_ = new StrictMockContainer(kContainerName);
  }

  // Start a no-op child process we can reap. The child will exit with
  // exit_code.
  pid_t StartChild(int exit_code) {
    SubProcess sp;
    sp.SetArgv({"/bin/sh", "-c", Substitute("exit $0", exit_code)});
    sp.Start();
    return sp.pid();
  }

 protected:
  RunSpec detached_spec_;
  const vector<string> argv_;
  unique_ptr<MockContainerApi> mock_lmctfy_;
  MockContainer *mock_container_;
};

TEST_F(RunExecTest, ForegroundSuccess) {
  OutputMap output;

  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillRepeatedly(Return(mock_container_));

  EXPECT_CALL(*mock_container_,
              Exec(ElementsAre(kArgv[0], kArgv[1])))
      .WillRepeatedly(Return(Status::OK));

  FLAGS_lmctfy_no_wait = false;
  EXPECT_OK(RunInContainer(argv_, mock_lmctfy_.get(), &output));
  EXPECT_EQ(0, output.NumPairs());
}

TEST_F(RunExecTest, ForegroundExecFails) {
  OutputMap output;

  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillRepeatedly(Return(mock_container_));

  EXPECT_CALL(*mock_container_,
              Exec(ElementsAre(kArgv[0], kArgv[1])))
      .WillRepeatedly(Return(Status::CANCELLED));

  FLAGS_lmctfy_no_wait = false;
  EXPECT_EQ(Status::CANCELLED,
            RunInContainer(argv_, mock_lmctfy_.get(), &output));
}

TEST_F(RunExecTest, ForegroundGetFails) {
  OutputMap output;

  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillRepeatedly(Return(Status::CANCELLED));

  FLAGS_lmctfy_no_wait = false;
  EXPECT_EQ(Status::CANCELLED,
            RunInContainer(argv_, mock_lmctfy_.get(), &output));

  // The mock container is never returned so delete it.
  delete mock_container_;
}

TEST_F(RunExecTest, BackgroundSuccess) {
  OutputMap output;

  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillRepeatedly(Return(mock_container_));

  EXPECT_CALL(*mock_container_, Run(ElementsAre(kArgv[0], kArgv[1]),
                                    EqualsInitializedProto(detached_spec_)))
      .WillRepeatedly(Return(42));

  FLAGS_lmctfy_no_wait = true;
  EXPECT_TRUE(RunInContainer(argv_, mock_lmctfy_.get(), &output).ok());
  ASSERT_EQ(1, output.NumPairs());
  EXPECT_TRUE(output.ContainsPair("pid", "42"));
}

TEST_F(RunExecTest, BackgroundRunFails) {
  OutputMap output;

  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillRepeatedly(Return(mock_container_));

  EXPECT_CALL(*mock_container_, Run(ElementsAre(kArgv[0], kArgv[1]),
                                    EqualsInitializedProto(detached_spec_)))
      .WillRepeatedly(Return(Status::CANCELLED));

  FLAGS_lmctfy_no_wait = true;
  EXPECT_EQ(Status::CANCELLED,
            RunInContainer(argv_, mock_lmctfy_.get(), &output));
}

TEST_F(RunExecTest, BackgroundGetFails) {
  OutputMap output;

  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillRepeatedly(Return(Status::CANCELLED));

  FLAGS_lmctfy_no_wait = true;
  EXPECT_EQ(Status::CANCELLED,
            RunInContainer(argv_, mock_lmctfy_.get(), &output));

  // The mock container is never returned so delete it.
  delete mock_container_;
}

class RunBashTest : public ::testing::Test {
 public:
  RunBashTest() : argv_({"run", kContainerName, kCmd}) {
    detached_spec_.set_fd_policy(RunSpec::DETACHED);
  }

  virtual void SetUp() {
    mock_lmctfy_.reset(new StrictMockContainerApi());
    mock_container_ = new StrictMockContainer(kContainerName);
  }

  // Start a no-op child process we can reap. The child will exit with
  // exit_code.
  pid_t StartChild(int exit_code) {
    SubProcess sp;
    sp.SetArgv({"/bin/sh", "-c", Substitute("exit $0", exit_code)});
    sp.Start();
    return sp.pid();
  }

 protected:
  RunSpec detached_spec_;
  const vector<string> argv_;
  unique_ptr<MockContainerApi> mock_lmctfy_;
  MockContainer *mock_container_;
};

TEST_F(RunBashTest, ForegroundSuccess) {
  OutputMap output;

  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillRepeatedly(Return(mock_container_));

  EXPECT_CALL(*mock_container_, Exec(ElementsAre("/bin/sh", "-c", kCmd)))
      .WillRepeatedly(Return(Status::OK));

  FLAGS_lmctfy_no_wait = false;
  EXPECT_OK(RunInContainer(argv_, mock_lmctfy_.get(), &output));
  EXPECT_EQ(0, output.NumPairs());
}

TEST_F(RunBashTest, ForegroundExecFails) {
  OutputMap output;

  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillRepeatedly(Return(mock_container_));

  EXPECT_CALL(*mock_container_,
              Exec(ElementsAre("/bin/sh", "-c", kCmd)))
      .WillRepeatedly(Return(Status::CANCELLED));

  FLAGS_lmctfy_no_wait = false;
  EXPECT_EQ(Status::CANCELLED,
            RunInContainer(argv_, mock_lmctfy_.get(), &output));
}

}  // namespace
}  // namespace cli
}  // namespace lmctfy
}  // namespace containers
