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

#include "lmctfy/cli/commands/create.h"

#include <memory>
#include <vector>

#include "gflags/gflags.h"
#include "file/memfile/inlinefile.h"
#include "lmctfy/cli/output_map.h"
#include "include/lmctfy.pb.h"
#include "include/lmctfy_mock.h"
#include "strings/substitute.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "util/task/codes.pb.h"

DECLARE_string(lmctfy_config);

using ::std::unique_ptr;
using ::std::vector;
using ::strings::Substitute;
#include "util/testing/equals_initialized_proto.h"
using ::testing::EqualsInitializedProto;
using ::testing::Return;
using ::util::Status;

namespace containers {
namespace lmctfy {
namespace cli {
namespace {

static const char kContainerName[] = "/test";

class CreateTest : public ::testing::Test {
 public:
  virtual void SetUp() {
    mock_lmctfy_.reset(new StrictMockContainerApi());
    mock_container_ = new StrictMockContainer(kContainerName);
  }

 protected:
  unique_ptr<MockContainerApi> mock_lmctfy_;
  MockContainer *mock_container_;
};

TEST_F(CreateTest, ConfigOnCommandLineEmpty) {
  const vector<string> args = {"create", kContainerName, ""};
  ContainerSpec spec;

  EXPECT_CALL(*mock_lmctfy_,
              Create(kContainerName, EqualsInitializedProto(spec)))
      .WillOnce(Return(mock_container_));

  FLAGS_lmctfy_config = "";
  EXPECT_TRUE(CreateContainer(args, mock_lmctfy_.get(), nullptr).ok());
}

TEST_F(CreateTest, ConfigOnCommandLineAscii) {
  const vector<string> args = {"create", kContainerName, "owner: 42"};
  ContainerSpec spec;
  spec.set_owner(42);

  EXPECT_CALL(*mock_lmctfy_,
              Create(kContainerName, EqualsInitializedProto(spec)))
      .WillOnce(Return(mock_container_));

  FLAGS_lmctfy_config = "";
  EXPECT_TRUE(CreateContainer(args, mock_lmctfy_.get(), nullptr).ok());
}

TEST_F(CreateTest, ConfigOnCommandLineBinary) {
  ContainerSpec spec;
  spec.set_owner(42);
  string serialized;
  spec.SerializeToString(&serialized);
  const vector<string> args = {"create", kContainerName, serialized};

  EXPECT_CALL(*mock_lmctfy_,
              Create(kContainerName, EqualsInitializedProto(spec)))
      .WillOnce(Return(mock_container_));

  FLAGS_lmctfy_config = "";
  EXPECT_TRUE(CreateContainer(args, mock_lmctfy_.get(), nullptr).ok());
}

TEST_F(CreateTest, ConfigOnCommandLineUnparsable) {
  // Delete the container since we never created it.
  unique_ptr<MockContainer> d(mock_container_);

  const vector<string> args = {"create", kContainerName, "unparsable"};

  FLAGS_lmctfy_config = "";
  Status status = CreateContainer(args, mock_lmctfy_.get(), nullptr);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(::util::error::INVALID_ARGUMENT, status.error_code());
}

TEST_F(CreateTest, ConfigOnFlagAscii) {
  const vector<string> args = {"create", kContainerName};
  ContainerSpec spec;
  spec.set_owner(42);

  EXPECT_CALL(*mock_lmctfy_,
              Create(kContainerName, EqualsInitializedProto(spec)))
      .WillOnce(Return(mock_container_));

  FLAGS_lmctfy_config = GetInlineFilename("owner: 42");
  EXPECT_TRUE(CreateContainer(args, mock_lmctfy_.get(), nullptr).ok());
}

TEST_F(CreateTest, ConfigOnFlagBinary) {
  const vector<string> args = {"create", kContainerName};
  ContainerSpec spec;
  spec.set_owner(42);
  string serialized;
  spec.SerializeToString(&serialized);

  EXPECT_CALL(*mock_lmctfy_,
              Create(kContainerName, EqualsInitializedProto(spec)))
      .WillOnce(Return(mock_container_));

  FLAGS_lmctfy_config = GetInlineFilename(serialized);
  EXPECT_TRUE(CreateContainer(args, mock_lmctfy_.get(), nullptr).ok());
}

TEST_F(CreateTest, ConfigOnFlagUnparsable) {
  // Delete the container since we never created it.
  unique_ptr<MockContainer> d(mock_container_);

  const vector<string> args = {"create", kContainerName};

  FLAGS_lmctfy_config = GetInlineFilename("unparsable");
  Status status = CreateContainer(args, mock_lmctfy_.get(), nullptr);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(::util::error::INVALID_ARGUMENT, status.error_code());
}

TEST_F(CreateTest, ConfigOnFlagFileOpenFails) {
  // Delete the container since we never created it.
  unique_ptr<MockContainer> d(mock_container_);

  const vector<string> args = {"create", kContainerName};

  FLAGS_lmctfy_config = "/this/file/does/not/exist";
  EXPECT_FALSE(CreateContainer(args, mock_lmctfy_.get(), nullptr).ok());
}

TEST_F(CreateTest, CreateContainerFails) {
  // Delete the container since we never created it.
  unique_ptr<MockContainer> d(mock_container_);

  const vector<string> args = {"create", kContainerName, ""};
  ContainerSpec spec;

  EXPECT_CALL(*mock_lmctfy_,
              Create(kContainerName, EqualsInitializedProto(spec)))
      .WillOnce(Return(Status::CANCELLED));

  FLAGS_lmctfy_config = "";
  EXPECT_EQ(Status::CANCELLED,
            CreateContainer(args, mock_lmctfy_.get(), nullptr));
}

TEST_F(CreateTest, CommandLineAndFlagSpecified) {
  // Delete the container since we never created it.
  unique_ptr<MockContainer> d(mock_container_);

  const vector<string> args = {"create", kContainerName, ""};

  FLAGS_lmctfy_config = "some_file";
  Status status = CreateContainer(args, mock_lmctfy_.get(), nullptr);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(::util::error::INVALID_ARGUMENT, status.error_code());
}

TEST_F(CreateTest, CommandLineAndFlagNotSpecified) {
  // Delete the container since we never created it.
  unique_ptr<MockContainer> d(mock_container_);

  const vector<string> args = {"create", kContainerName};

  FLAGS_lmctfy_config = "";
  Status status = CreateContainer(args, mock_lmctfy_.get(), nullptr);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(::util::error::INVALID_ARGUMENT, status.error_code());
}

TEST_F(CreateTest, ReturnsInitPid) {
  ContainerSpec spec;
  spec.mutable_virtual_host();
  string serialized;
  spec.SerializeToString(&serialized);
  const vector<string> args = {"create", kContainerName, serialized};

  const pid_t kInitPid = 1;

  EXPECT_CALL(*mock_lmctfy_,
              Create(kContainerName, EqualsInitializedProto(spec)))
      .WillOnce(Return(mock_container_));

  EXPECT_CALL(*mock_container_, GetInitPid())
      .WillOnce(Return(kInitPid));

  OutputMap output;
  EXPECT_TRUE(CreateContainer(args, mock_lmctfy_.get(), &output).ok());

  EXPECT_TRUE(output.ContainsPair("init_pid", SimpleItoa(kInitPid)))
      << "Expected to find 'Init PID' in the output map of create.";
}

}  // namespace
}  // namespace cli
}  // namespace lmctfy
}  // namespace containers
