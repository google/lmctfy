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

#include "lmctfy/cli/commands/stats.h"

#include <memory>
#include <vector>

#include "gflags/gflags.h"
#include "include/lmctfy.h"
#include "include/lmctfy.pb.h"
#include "include/lmctfy_mock.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

DECLARE_bool(lmctfy_binary);

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

class StatsTest : public ::testing::Test {
 public:
  virtual void SetUp() {
    mock_lmctfy_.reset(new StrictMockContainerApi());
    mock_container_ = new StrictMockContainer(kContainerName);
  }

 protected:
  unique_ptr<MockContainerApi> mock_lmctfy_;
  MockContainer *mock_container_;
  OutputMap output_maps_;
};

TEST_F(StatsTest, SummarySuccess) {
  const vector<string> args = {"summary", kContainerName};
  ContainerStats stats;

  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillOnce(Return(mock_container_));
  EXPECT_CALL(*mock_container_, Stats(Container::STATS_SUMMARY))
      .WillRepeatedly(Return(stats));

  FLAGS_lmctfy_binary = false;
  EXPECT_TRUE(StatsSummary(args, mock_lmctfy_.get(), &output_maps_).ok());
}

TEST_F(StatsTest, SummarySuccessBinary) {
  const vector<string> args = {"summary", kContainerName};
  ContainerStats stats;

  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillOnce(Return(mock_container_));
  EXPECT_CALL(*mock_container_, Stats(Container::STATS_SUMMARY))
      .WillRepeatedly(Return(stats));

  FLAGS_lmctfy_binary = true;
  EXPECT_TRUE(StatsSummary(args, mock_lmctfy_.get(), &output_maps_).ok());
}

TEST_F(StatsTest, SummarySuccessSelf) {
  const vector<string> args = {"summary", kContainerName};
  ContainerStats stats;

  EXPECT_CALL(*mock_lmctfy_, Detect(Ge(0)))
      .WillRepeatedly(Return(string(kContainerName)));
  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillOnce(Return(mock_container_));
  EXPECT_CALL(*mock_container_, Stats(Container::STATS_SUMMARY))
      .WillRepeatedly(Return(stats));

  FLAGS_lmctfy_binary = false;
  EXPECT_TRUE(StatsSummary(args, mock_lmctfy_.get(), &output_maps_).ok());
}

TEST_F(StatsTest, SummarySelfDetectFails) {
  const vector<string> args = {"summary"};

  EXPECT_CALL(*mock_lmctfy_, Detect(Ge(0)))
      .WillRepeatedly(Return(Status::CANCELLED));

  FLAGS_lmctfy_binary = false;
  EXPECT_EQ(Status::CANCELLED,
            StatsSummary(args, mock_lmctfy_.get(), &output_maps_));
}

TEST_F(StatsTest, SummaryGetFails) {
  const vector<string> args = {"summary", kContainerName};

  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillOnce(Return(Status::CANCELLED));

  FLAGS_lmctfy_binary = false;
  EXPECT_EQ(Status::CANCELLED,
            StatsSummary(args, mock_lmctfy_.get(), &output_maps_));

  // Delete the container since it is never returned.
  delete mock_container_;
}

TEST_F(StatsTest, SummaryStatsFails) {
  const vector<string> args = {"summary", kContainerName};

  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillOnce(Return(mock_container_));
  EXPECT_CALL(*mock_container_, Stats(Container::STATS_SUMMARY))
      .WillRepeatedly(Return(Status::CANCELLED));

  FLAGS_lmctfy_binary = false;
  EXPECT_EQ(Status::CANCELLED,
            StatsSummary(args, mock_lmctfy_.get(), &output_maps_));
}

TEST_F(StatsTest, FullSuccess) {
  const vector<string> args = {"full", kContainerName};
  ContainerStats stats;

  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillOnce(Return(mock_container_));
  EXPECT_CALL(*mock_container_, Stats(Container::STATS_FULL))
      .WillRepeatedly(Return(stats));

  FLAGS_lmctfy_binary = false;
  EXPECT_TRUE(StatsFull(args, mock_lmctfy_.get(), &output_maps_).ok());
}

TEST_F(StatsTest, FullSuccessBinary) {
  const vector<string> args = {"full", kContainerName};
  ContainerStats stats;

  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillOnce(Return(mock_container_));
  EXPECT_CALL(*mock_container_, Stats(Container::STATS_FULL))
      .WillRepeatedly(Return(stats));

  FLAGS_lmctfy_binary = true;
  EXPECT_TRUE(StatsFull(args, mock_lmctfy_.get(), &output_maps_).ok());
}

TEST_F(StatsTest, FullSuccessSelf) {
  const vector<string> args = {"full", kContainerName};
  ContainerStats stats;

  EXPECT_CALL(*mock_lmctfy_, Detect(Ge(0)))
      .WillRepeatedly(Return(string(kContainerName)));
  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillOnce(Return(mock_container_));
  EXPECT_CALL(*mock_container_, Stats(Container::STATS_FULL))
      .WillRepeatedly(Return(stats));

  FLAGS_lmctfy_binary = false;
  EXPECT_TRUE(StatsFull(args, mock_lmctfy_.get(), &output_maps_).ok());
}

TEST_F(StatsTest, FullSelfDetectFails) {
  const vector<string> args = {"full"};

  EXPECT_CALL(*mock_lmctfy_, Detect(Ge(0)))
      .WillRepeatedly(Return(Status::CANCELLED));

  FLAGS_lmctfy_binary = false;
  EXPECT_EQ(Status::CANCELLED,
            StatsFull(args, mock_lmctfy_.get(), &output_maps_));
}

TEST_F(StatsTest, FullGetFails) {
  const vector<string> args = {"full", kContainerName};

  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillOnce(Return(Status::CANCELLED));

  FLAGS_lmctfy_binary = false;
  EXPECT_EQ(Status::CANCELLED,
            StatsFull(args, mock_lmctfy_.get(), &output_maps_));

  // Delete the container since it is never returned.
  delete mock_container_;
}

TEST_F(StatsTest, FullStatsFails) {
  const vector<string> args = {"full", kContainerName};

  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillOnce(Return(mock_container_));
  EXPECT_CALL(*mock_container_, Stats(Container::STATS_FULL))
      .WillRepeatedly(Return(Status::CANCELLED));

  FLAGS_lmctfy_binary = false;
  EXPECT_EQ(Status::CANCELLED,
            StatsFull(args, mock_lmctfy_.get(), &output_maps_));
}

}  // namespace
}  // namespace cli
}  // namespace lmctfy
}  // namespace containers
