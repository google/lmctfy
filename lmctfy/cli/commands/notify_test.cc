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

#include "lmctfy/cli/commands/notify.h"

#include <memory>
#include <vector>

#include "gflags/gflags.h"
#include "lmctfy/cli/output_map.h"
#include "include/lmctfy.pb.h"
#include "include/lmctfy_mock.h"
#include "util/errors_test_util.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "util/task/codes.pb.h"

DECLARE_string(lmctfy_config);

using ::std::unique_ptr;
using ::std::vector;
using ::testing::DoAll;
#include "util/testing/equals_initialized_proto.h"
using ::testing::EqualsInitializedProto;
using ::testing::Invoke;
using ::testing::NotNull;
using ::testing::Return;
using ::util::Status;

namespace containers {
namespace lmctfy {
namespace cli {
namespace {

static const char kContainerName[] = "/test";

class NotifyTest : public ::testing::Test {
 public:
  virtual void SetUp() {
    mock_lmctfy_.reset(new StrictMockContainerApi());
    mock_container_ = new StrictMockContainer(kContainerName);
  }

 protected:
  unique_ptr<MockContainerApi> mock_lmctfy_;
  MockContainer *mock_container_;
  OutputMap output_;
};

// Run the callback on a nullptr container with Status::OK.
void RunCallback(const EventSpec &spec, Container::EventCallback *cb) {
  cb->Run(nullptr, Status::OK);
}

// Delete the specified callback.
void DeleteCallback(const EventSpec &spec, Container::EventCallback *cb) {
  delete cb;
}

TEST_F(NotifyTest, MemoryThresholdSuccess) {
  const vector<string> args = {"threshold", kContainerName, "4096"};
  EventSpec spec;
  spec.mutable_memory_threshold()->set_usage(4096);

  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillOnce(Return(mock_container_));
  EXPECT_CALL(*mock_container_,
              RegisterNotification(EqualsInitializedProto(spec), NotNull()))
      .WillOnce(
           DoAll(Invoke(&RunCallback), Invoke(&DeleteCallback), Return(1)));

  EXPECT_OK(MemoryThresholdHandler(args, mock_lmctfy_.get(), &output_));
}

TEST_F(NotifyTest, MemoryThresholdBadThreshold) {
  // Delete the container since we never get it.
  unique_ptr<MockContainer> d(mock_container_);

  const vector<string> args = {"threshold", kContainerName, "NaN"};

  EXPECT_ERROR_CODE(
      ::util::error::INVALID_ARGUMENT,
      MemoryThresholdHandler(args, mock_lmctfy_.get(), &output_));
}

TEST_F(NotifyTest, MemoryThresholdGetFails) {
  // Delete the container since we never get it.
  unique_ptr<MockContainer> d(mock_container_);

  const vector<string> args = {"threshold", kContainerName, "4096"};

  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillOnce(Return(Status::CANCELLED));

  EXPECT_NOT_OK(MemoryThresholdHandler(args, mock_lmctfy_.get(), &output_));
}

TEST_F(NotifyTest, MemoryThresholdRegisterFails) {
  const vector<string> args = {"threshold", kContainerName, "4096"};
  EventSpec spec;
  spec.mutable_memory_threshold()->set_usage(4096);

  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillOnce(Return(mock_container_));
  EXPECT_CALL(*mock_container_,
              RegisterNotification(EqualsInitializedProto(spec), NotNull()))
      .WillOnce(DoAll(Invoke(&DeleteCallback), Return(Status::CANCELLED)));

  EXPECT_NOT_OK(MemoryThresholdHandler(args, mock_lmctfy_.get(), &output_));
}

TEST_F(NotifyTest, MemoryOomSuccess) {
  const vector<string> args = {"oom", kContainerName};
  EventSpec spec;
  spec.mutable_oom();

  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillOnce(Return(mock_container_));
  EXPECT_CALL(*mock_container_,
              RegisterNotification(EqualsInitializedProto(spec), NotNull()))
      .WillOnce(
           DoAll(Invoke(&RunCallback), Invoke(&DeleteCallback), Return(1)));

  EXPECT_OK(MemoryOomHandler(args, mock_lmctfy_.get(), &output_));
}

TEST_F(NotifyTest, MemoryOomGetFails) {
  // Delete the container since we never get it.
  unique_ptr<MockContainer> d(mock_container_);

  const vector<string> args = {"oom", kContainerName};

  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillOnce(Return(Status::CANCELLED));

  EXPECT_NOT_OK(MemoryOomHandler(args, mock_lmctfy_.get(), &output_));
}

TEST_F(NotifyTest, MemoryOomRegisterFails) {
  const vector<string> args = {"oom", kContainerName};
  EventSpec spec;
  spec.mutable_oom();

  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillOnce(Return(mock_container_));
  EXPECT_CALL(*mock_container_,
              RegisterNotification(EqualsInitializedProto(spec), NotNull()))
      .WillOnce(DoAll(Invoke(&DeleteCallback), Return(Status::CANCELLED)));

  EXPECT_NOT_OK(MemoryOomHandler(args, mock_lmctfy_.get(), &output_));
}

}  // namespace
}  // namespace cli
}  // namespace lmctfy
}  // namespace containers
