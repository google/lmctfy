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

#include "lmctfy/cli/commands/destroy.h"

#include <sys/types.h>
#include <memory>
#include <vector>

#include "gflags/gflags.h"
#include "include/lmctfy.h"
#include "include/lmctfy_mock.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "util/task/codes.pb.h"

DECLARE_bool(lmctfy_force);

using ::std::unique_ptr;
using ::std::vector;
using ::testing::Return;
using ::util::Status;

namespace containers {
namespace lmctfy {
namespace cli {
namespace {

static const char kContainerName[] = "/test";

class DestroyTest : public ::testing::Test {
 public:
  DestroyTest() : args_({"destroy", kContainerName}) {
  }

  virtual void SetUp() {
    mock_lmctfy_.reset(new StrictMockContainerApi());
    mock_container_.reset(new StrictMockContainer(kContainerName));
  }

 protected:
  const vector<string> args_;
  unique_ptr<MockContainerApi> mock_lmctfy_;
  unique_ptr<MockContainer> mock_container_;
};

TEST_F(DestroyTest, SuccessForce) {
  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillRepeatedly(Return(mock_container_.get()));
  EXPECT_CALL(*mock_lmctfy_, Destroy(mock_container_.get()))
      .WillOnce(Return(Status::OK));

  FLAGS_lmctfy_force = true;
  EXPECT_TRUE(DestroyContainer(args_, mock_lmctfy_.get(), nullptr).ok());
}

TEST_F(DestroyTest, SuccessNonForce) {
  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillRepeatedly(Return(mock_container_.get()));
  EXPECT_CALL(*mock_lmctfy_, Destroy(mock_container_.get()))
      .WillOnce(Return(Status::OK));

  EXPECT_CALL(*mock_container_, ListSubcontainers(Container::LIST_SELF))
      .WillRepeatedly(Return(vector<Container *>()));
  EXPECT_CALL(*mock_container_, ListProcesses(Container::LIST_SELF))
      .WillRepeatedly(Return(vector<pid_t>()));
  EXPECT_CALL(*mock_container_, ListThreads(Container::LIST_SELF))
      .WillRepeatedly(Return(vector<pid_t>()));

  FLAGS_lmctfy_force = false;
  EXPECT_TRUE(DestroyContainer(args_, mock_lmctfy_.get(), nullptr).ok());
}

TEST_F(DestroyTest, ContainerDoesNotExist) {
  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillRepeatedly(Return(Status(::util::error::NOT_FOUND, "")));

  Status status;

  // Force
  FLAGS_lmctfy_force = true;
  status = DestroyContainer(args_, mock_lmctfy_.get(), nullptr);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(::util::error::NOT_FOUND, status.error_code());

  // Non-force
  FLAGS_lmctfy_force = false;
  status = DestroyContainer(args_, mock_lmctfy_.get(), nullptr);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(::util::error::NOT_FOUND, status.error_code());
}

TEST_F(DestroyTest, ContainerForceDestroyFails) {
  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillRepeatedly(Return(mock_container_.get()));
  EXPECT_CALL(*mock_lmctfy_, Destroy(mock_container_.get()))
      .WillOnce(Return(Status::CANCELLED));

  FLAGS_lmctfy_force = true;
  EXPECT_EQ(Status::CANCELLED,
            DestroyContainer(args_, mock_lmctfy_.get(), nullptr));
}

TEST_F(DestroyTest, ContainerNonForceDestroyFails) {
  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillRepeatedly(Return(mock_container_.get()));
  EXPECT_CALL(*mock_lmctfy_, Destroy(mock_container_.get()))
      .WillOnce(Return(Status::CANCELLED));

  EXPECT_CALL(*mock_container_, ListSubcontainers(Container::LIST_SELF))
      .WillRepeatedly(Return(vector<Container *>()));
  EXPECT_CALL(*mock_container_, ListProcesses(Container::LIST_SELF))
      .WillRepeatedly(Return(vector<pid_t>()));
  EXPECT_CALL(*mock_container_, ListThreads(Container::LIST_SELF))
      .WillRepeatedly(Return(vector<pid_t>()));

  FLAGS_lmctfy_force = false;
  EXPECT_EQ(Status::CANCELLED,
            DestroyContainer(args_, mock_lmctfy_.get(), nullptr));
}

TEST_F(DestroyTest, ContainerForceWithSubcontainers) {
  Container *sub1 = new StrictMockContainer("/test/sub1");
  Container *sub2 = new StrictMockContainer("/test/sub2");

  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillRepeatedly(Return(mock_container_.get()));

  EXPECT_CALL(*mock_container_, ListSubcontainers(Container::LIST_SELF))
      .WillRepeatedly(Return(vector<Container *>({sub1, sub2})));
  EXPECT_CALL(*mock_container_, ListProcesses(Container::LIST_SELF))
      .WillRepeatedly(Return(vector<pid_t>()));
  EXPECT_CALL(*mock_container_, ListThreads(Container::LIST_SELF))
      .WillRepeatedly(Return(vector<pid_t>()));

  FLAGS_lmctfy_force = false;
  Status status = DestroyContainer(args_, mock_lmctfy_.get(), nullptr);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(::util::error::FAILED_PRECONDITION, status.error_code());

  // Destroy not called, command should free container.
  mock_container_.release();
}

TEST_F(DestroyTest, ContainerForceWithPids) {
  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillRepeatedly(Return(mock_container_.get()));

  EXPECT_CALL(*mock_container_, ListSubcontainers(Container::LIST_SELF))
      .WillRepeatedly(Return(vector<Container *>()));
  EXPECT_CALL(*mock_container_, ListProcesses(Container::LIST_SELF))
      .WillRepeatedly(Return(vector<pid_t>({1, 2, 3})));
  EXPECT_CALL(*mock_container_, ListThreads(Container::LIST_SELF))
      .WillRepeatedly(Return(vector<pid_t>()));

  FLAGS_lmctfy_force = false;
  Status status = DestroyContainer(args_, mock_lmctfy_.get(), nullptr);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(::util::error::FAILED_PRECONDITION, status.error_code());

  // Destroy not called, command should free container.
  mock_container_.release();
}

TEST_F(DestroyTest, ContainerForceWithTids) {
  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillRepeatedly(Return(mock_container_.get()));

  EXPECT_CALL(*mock_container_, ListSubcontainers(Container::LIST_SELF))
      .WillRepeatedly(Return(vector<Container *>()));
  EXPECT_CALL(*mock_container_, ListProcesses(Container::LIST_SELF))
      .WillRepeatedly(Return(vector<pid_t>()));
  EXPECT_CALL(*mock_container_, ListThreads(Container::LIST_SELF))
      .WillRepeatedly(Return(vector<pid_t>({1, 2, 3})));

  FLAGS_lmctfy_force = false;
  Status status = DestroyContainer(args_, mock_lmctfy_.get(), nullptr);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(::util::error::FAILED_PRECONDITION, status.error_code());

  // Destroy not called, command should free container.
  mock_container_.release();
}

}  // namespace
}  // namespace cli
}  // namespace lmctfy
}  // namespace containers
