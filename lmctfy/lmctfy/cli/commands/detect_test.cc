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

#include "lmctfy/cli/commands/detect.h"

#include <memory>
#include <vector>

#include "lmctfy/cli/output_map.h"
#include "include/lmctfy_mock.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "util/task/codes.pb.h"

using ::std::unique_ptr;
using ::std::vector;
using ::testing::Ge;
using ::testing::Return;
using ::util::Status;

namespace containers {
namespace lmctfy {
namespace cli {
namespace {

static const char kContainerName[] = "/test";

class DetectTest : public ::testing::Test {
 public:
  virtual void SetUp() {
    mock_lmctfy_.reset(new StrictMockContainerApi());
  }

 protected:
  unique_ptr<MockContainerApi> mock_lmctfy_;
};

TEST_F(DetectTest, Success) {
  const vector<string> args = {"detect", "42"};
  OutputMap output;

  EXPECT_CALL(*mock_lmctfy_, Detect(42))
      .WillOnce(Return(string(kContainerName)));

  EXPECT_TRUE(DetectContainer(args, mock_lmctfy_.get(), &output).ok());
  ASSERT_EQ(1, output.NumPairs());
  EXPECT_TRUE(output.ContainsPair("name", kContainerName));
}

TEST_F(DetectTest, SuccessSelf) {
  const vector<string> args = {"detect"};
  OutputMap output;

  EXPECT_CALL(*mock_lmctfy_, Detect(Ge(0)))
      .WillOnce(Return(string(kContainerName)));

  EXPECT_TRUE(DetectContainer(args, mock_lmctfy_.get(), &output).ok());
  ASSERT_EQ(1, output.NumPairs());
  EXPECT_TRUE(output.ContainsPair("name", kContainerName));
}

TEST_F(DetectTest, BadPid) {
  const vector<string> args = {"detect", "not_a_pid"};
  OutputMap output;

  Status status = DetectContainer(args, mock_lmctfy_.get(), &output);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(::util::error::INVALID_ARGUMENT, status.error_code());
}

TEST_F(DetectTest, DetectFails) {
  const vector<string> args = {"detect", "42"};
  OutputMap output;

  EXPECT_CALL(*mock_lmctfy_, Detect(42))
      .WillOnce(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED,
            DetectContainer(args, mock_lmctfy_.get(), &output));
}

}  // namespace
}  // namespace cli
}  // namespace lmctfy
}  // namespace containers
