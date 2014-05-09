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

#include "lmctfy/cli/commands/enter.h"

#include <memory>
#include <vector>

#include "include/lmctfy_mock.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "util/task/codes.pb.h"
#include "util/task/statusor.h"

using ::std::unique_ptr;
using ::std::vector;
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

class EnterTest : public ::testing::Test {
 public:
  virtual void SetUp() {
    mock_lmctfy_.reset(new StrictMockContainerApi());
    mock_container_ = new StrictMockContainer(kContainerName);
  }

 protected:
  unique_ptr<MockContainerApi> mock_lmctfy_;
  MockContainer *mock_container_;
};

TEST_F(EnterTest, SuccessSelf) {
  const vector<string> argv = {"enter", kContainerName};

  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillRepeatedly(Return(mock_container_));

  EXPECT_CALL(*mock_container_, Enter(ElementsAre(Ge(0))))
      .WillRepeatedly(Return(Status::OK));

  EXPECT_TRUE(EnterContainer(argv, mock_lmctfy_.get(), nullptr).ok());
}

TEST_F(EnterTest, SuccessOneTid) {
  const vector<string> argv = {"enter", kContainerName, "42"};

  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillRepeatedly(Return(mock_container_));

  EXPECT_CALL(*mock_container_, Enter(ElementsAre(42)))
      .WillRepeatedly(Return(Status::OK));

  EXPECT_TRUE(EnterContainer(argv, mock_lmctfy_.get(), nullptr).ok());
}

TEST_F(EnterTest, SuccessMultipleTids) {
  const vector<string> argv = {"enter", kContainerName, "1", "2", "3", "4"};

  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillRepeatedly(Return(mock_container_));

  EXPECT_CALL(*mock_container_, Enter(WhenSorted(ElementsAre(1, 2, 3, 4))))
      .WillRepeatedly(Return(Status::OK));

  EXPECT_TRUE(EnterContainer(argv, mock_lmctfy_.get(), nullptr).ok());
}

TEST_F(EnterTest, BadTid) {
  const vector<string> argv = {"enter", kContainerName, "not_a_pid"};

  Status status = EnterContainer(argv, mock_lmctfy_.get(), nullptr);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(::util::error::FAILED_PRECONDITION, status.error_code());
}

TEST_F(EnterTest, GetContainerFails) {
  const vector<string> argv = {"enter", kContainerName, "42"};

  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED,
            EnterContainer(argv, mock_lmctfy_.get(), nullptr));

  // Delete the container since we did not create it.
  delete mock_container_;
}

TEST_F(EnterTest, EnterFails) {
  const vector<string> argv = {"enter", kContainerName, "42"};

  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillRepeatedly(Return(mock_container_));

  EXPECT_CALL(*mock_container_, Enter(ElementsAre(42)))
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED,
            EnterContainer(argv, mock_lmctfy_.get(), nullptr));
}

}  // namespace
}  // namespace cli
}  // namespace lmctfy
}  // namespace containers
