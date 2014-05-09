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

#include "lmctfy/cli/commands/update.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <memory>
#include <vector>

#include "gflags/gflags.h"
#include "file/memfile/inlinefile.h"
#include "include/lmctfy.pb.h"
#include "include/lmctfy_mock.h"
#include "util/errors_test_util.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "util/task/codes.pb.h"

DECLARE_string(lmctfy_config);

using ::std::unique_ptr;
using ::std::vector;
#include "util/testing/equals_initialized_proto.h"
using ::testing::EqualsInitializedProto;
using ::testing::Eq;
using ::testing::Ge;
using ::testing::Return;
using ::testing::_;
using ::util::Status;

namespace containers {
namespace lmctfy {
namespace cli {
namespace {

static const char kContainerName[] = "/test";

void ExpectedPolicyForCommand(const string &command,
                              Container::UpdatePolicy *expected_policy) {
  if (command == "diff") {
    *expected_policy = Container::UPDATE_DIFF;
  } else if (command == "replace") {
    *expected_policy = Container::UPDATE_REPLACE;
  } else {
    FAIL() << "Unexpected command: " << command;
  }
}

class UpdateTest : public ::testing::TestWithParam<string> {
 public:
  virtual void SetUp() {
    mock_lmctfy_.reset(new StrictMockContainerApi());
    mock_container_.reset(new StrictMockContainer(kContainerName));
    ExpectedPolicyForCommand(GetParam(), &expected_policy_);
    command_ = GetParam();
  }

  // Check whether the specified file exists.
  bool FileExists(const string &filename) {
    struct stat data;
    return stat(filename.c_str(), &data) == 0;
  }

 protected:
  unique_ptr<MockContainerApi> mock_lmctfy_;
  unique_ptr<MockContainer> mock_container_;
  Container::UpdatePolicy expected_policy_;
  string command_;
};

INSTANTIATE_TEST_CASE_P(Instantiation,
                          UpdateTest,
                          ::testing::Values("diff", "replace"));

TEST_P(UpdateTest, ConfigOnCommandLineEmpty) {
  const vector<string> args = {command_, kContainerName, ""};
  ContainerSpec spec;

  EXPECT_CALL(*mock_container_, Update(EqualsInitializedProto(spec),
                                       expected_policy_))
      .WillRepeatedly(Return(Status::OK));
  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillOnce(Return(mock_container_.release()));

  EXPECT_OK(UpdateContainer(expected_policy_,
                            args,
                            mock_lmctfy_.get(),
                            nullptr));
}

TEST_P(UpdateTest, ConfigOnCommandLineAscii) {
  const vector<string> args = {command_, kContainerName, "owner: 42\n"};
  ContainerSpec spec;
  spec.set_owner(42);

  EXPECT_CALL(*mock_container_, Update(EqualsInitializedProto(spec),
                                       expected_policy_))
      .WillRepeatedly(Return(Status::OK));
  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillOnce(Return(mock_container_.release()));

  EXPECT_OK(UpdateContainer(expected_policy_,
                            args,
                            mock_lmctfy_.get(),
                            nullptr));
}

TEST_P(UpdateTest, ConfigOnCommandLineBinary) {
  ContainerSpec spec;
  spec.set_owner(42);
  string serialized;
  spec.SerializeToString(&serialized);
  const vector<string> args = {command_, kContainerName, serialized};

  EXPECT_CALL(*mock_container_, Update(EqualsInitializedProto(spec),
                                       expected_policy_))
      .WillRepeatedly(Return(Status::OK));
  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillOnce(Return(mock_container_.release()));

  EXPECT_OK(UpdateContainer(expected_policy_,
                            args,
                            mock_lmctfy_.get(),
                            nullptr));
}

TEST_P(UpdateTest, ConfigOnCommandLineUnparsable) {
  const vector<string> args = {command_, kContainerName, "unparsable"};

  EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT,
                    UpdateContainer(expected_policy_,
                                    args,
                                    mock_lmctfy_.get(),
                                    nullptr));
}

TEST_P(UpdateTest, ConfigOnFlagAscii) {
  const vector<string> args = {command_, kContainerName};
  ContainerSpec spec;
  spec.set_owner(42);

  EXPECT_CALL(*mock_container_, Update(EqualsInitializedProto(spec),
                                       expected_policy_))
      .WillRepeatedly(Return(Status::OK));
  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillOnce(Return(mock_container_.release()));

  FLAGS_lmctfy_config = GetInlineFilename("owner: 42");
  EXPECT_OK(UpdateContainer(expected_policy_,
                            args,
                            mock_lmctfy_.get(),
                            nullptr));
}

TEST_P(UpdateTest, ConfigOnFlagBinary) {
  const vector<string> args = {command_, kContainerName};
  ContainerSpec spec;
  spec.set_owner(42);
  string serialized;
  spec.SerializeToString(&serialized);

  EXPECT_CALL(*mock_container_, Update(EqualsInitializedProto(spec),
                                       expected_policy_))
      .WillRepeatedly(Return(Status::OK));
  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillOnce(Return(mock_container_.release()));

  FLAGS_lmctfy_config = GetInlineFilename(serialized);
  EXPECT_OK(UpdateContainer(expected_policy_,
                            args,
                            mock_lmctfy_.get(),
                            nullptr));
}

TEST_P(UpdateTest, ConfigOnFlagUnparsable) {
  const vector<string> args = {command_, kContainerName};

  FLAGS_lmctfy_config = GetInlineFilename("unparsable");
  EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT,
                    UpdateContainer(expected_policy_,
                                    args,
                                    mock_lmctfy_.get(),
                                    nullptr));
}

TEST_P(UpdateTest, ConfigOnFlagFileOpenFails) {
  const vector<string> args = {command_, kContainerName};

  FLAGS_lmctfy_config = "/this/file/does/not/exist";
  ASSERT_FALSE(FileExists(FLAGS_lmctfy_config));
  EXPECT_NOT_OK(UpdateContainer(expected_policy_,
                                args,
                                mock_lmctfy_.get(),
                                nullptr));
}

TEST_P(UpdateTest, UpdateContainerFails) {
  const vector<string> args = {command_, kContainerName, ""};
  ContainerSpec spec;

  EXPECT_CALL(*mock_container_, Update(EqualsInitializedProto(spec),
                                       expected_policy_))
      .WillOnce(Return(Status(::util::error::INTERNAL, "")));
  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillOnce(Return(mock_container_.release()));

  FLAGS_lmctfy_config = "";
  EXPECT_ERROR_CODE(::util::error::INTERNAL,
                    UpdateContainer(expected_policy_,
                                    args,
                                    mock_lmctfy_.get(),
                                    nullptr));
}
TEST_P(UpdateTest, GetContainerFails) {
  const vector<string> args = {command_, kContainerName, ""};
  ContainerSpec spec;

  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillOnce(Return(Status(::util::error::INTERNAL, "")));

  EXPECT_ERROR_CODE(::util::error::INTERNAL,
                    UpdateContainer(expected_policy_,
                                    args,
                                    mock_lmctfy_.get(),
                                    nullptr));
}

TEST_P(UpdateTest, CommandLineAndFlagSpecified) {
  const vector<string> args = {command_, kContainerName, ""};

  FLAGS_lmctfy_config = "some_file";
  EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT,
                    UpdateContainer(expected_policy_,
                                    args,
                                    mock_lmctfy_.get(),
                                    nullptr));
}

TEST_P(UpdateTest, CommandLineAndFlagNotSpecified) {
  const vector<string> args = {command_, kContainerName};

  FLAGS_lmctfy_config = "";
  EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT,
                    UpdateContainer(expected_policy_,
                                    args,
                                    mock_lmctfy_.get(),
                                    nullptr));
}

}  // namespace
}  // namespace cli
}  // namespace lmctfy
}  // namespace containers
