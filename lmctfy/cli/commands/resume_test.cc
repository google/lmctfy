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

#include "lmctfy/cli/commands/resume.h"

#include <memory>
#include <string>
using ::std::string;
#include <vector>

#include "include/lmctfy.h"
#include "include/lmctfy_mock.h"
#include "util/errors_test_util.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "util/task/status.h"

using ::std::unique_ptr;
using ::std::vector;
using ::testing::Return;
using ::util::Status;

namespace containers {
namespace lmctfy {
namespace cli {
namespace {

static const char kContainerName[] = "/test";

class ResumeTest : public ::testing::Test {
 public:
  ResumeTest() : args_({"resume", kContainerName}) {
  }

  void SetUp() override {
    mock_lmctfy_.reset(new StrictMockContainerApi());
    mock_container_.reset(new StrictMockContainer(kContainerName));
  }

 protected:
  const vector<string> args_;
  unique_ptr<MockContainerApi> mock_lmctfy_;
  unique_ptr<MockContainer> mock_container_;
};

TEST_F(ResumeTest, Success) {
  EXPECT_CALL(*mock_container_, Resume())
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillOnce(Return(mock_container_.release()));

  EXPECT_OK(ResumeContainer(args_, mock_lmctfy_.get(), nullptr));
}

TEST_F(ResumeTest, GetFailure) {
  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillRepeatedly(Return(Status::CANCELLED));
  EXPECT_EQ(Status::CANCELLED,
            ResumeContainer(args_, mock_lmctfy_.get(), nullptr));
}

TEST_F(ResumeTest, ResumeFailure) {
  EXPECT_CALL(*mock_container_, Resume())
      .WillOnce(Return(Status::CANCELLED));
  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillOnce(Return(mock_container_.release()));
  EXPECT_EQ(Status::CANCELLED,
            ResumeContainer(args_, mock_lmctfy_.get(), nullptr));
}

}  // namespace
}  // namespace cli
}  // namespace lmctfy
}  // namespace containers
