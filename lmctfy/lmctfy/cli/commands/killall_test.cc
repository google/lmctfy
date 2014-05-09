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

#include "lmctfy/cli/commands/killall.h"

#include <memory>
#include <vector>

#include "include/lmctfy_mock.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

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

class KillAllTest : public ::testing::Test {
 public:
  KillAllTest() : args_({"killall", kContainerName}) {}

  virtual void SetUp() {
    mock_lmctfy_.reset(new StrictMockContainerApi());
    mock_container_ = new StrictMockContainer(kContainerName);
  }

 protected:
  const vector<string> args_;
  unique_ptr<MockContainerApi> mock_lmctfy_;
  MockContainer *mock_container_;
};

TEST_F(KillAllTest, Success) {
  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillRepeatedly(Return(mock_container_));
  EXPECT_CALL(*mock_container_, KillAll())
      .WillOnce(Return(Status::OK));

  EXPECT_TRUE(KillAllInContainer(args_, mock_lmctfy_.get(), nullptr).ok());
}

TEST_F(KillAllTest, GetFails) {
  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED,
            KillAllInContainer(args_, mock_lmctfy_.get(), nullptr));

  // Delete the container since it is not returned by Get().
  delete mock_container_;
}

TEST_F(KillAllTest, KillAllFails) {
  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillRepeatedly(Return(mock_container_));
  EXPECT_CALL(*mock_container_, KillAll())
      .WillOnce(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED,
            KillAllInContainer(args_, mock_lmctfy_.get(), nullptr));
}

}  // namespace
}  // namespace cli
}  // namespace lmctfy
}  // namespace containers
