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

#include "lmctfy/cli/commands/list.h"

#include <sys/types.h>
#include <memory>
#include <vector>

#include "gflags/gflags.h"
#include "lmctfy/cli/output_map.h"
#include "include/lmctfy.h"
#include "include/lmctfy_mock.h"
#include "strings/substitute.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

DECLARE_bool(lmctfy_recursive);

using ::std::unique_ptr;
using ::std::vector;
using ::strings::Substitute;
using ::testing::Ge;
using ::testing::Return;
using ::util::Status;

namespace containers {
namespace lmctfy {
namespace cli {
namespace {

static const char kContainerName[] = "/test";
static const char kSubName1[] = "/test/sub1";
static const char kSubName2[] = "/test/sub2";

class ListTest : public ::testing::Test {
 public:
  virtual void SetUp() {
    mock_lmctfy_.reset(new StrictMockContainerApi());
    mock_container_ = new StrictMockContainer(kContainerName);
  }

  // Expect the output to contain the specified container names. The container
  // name should be under the "name" key in the output maps.
  void ExpectContainers(const vector<string> &expected_containers) {
    // Ensure all containers are in the output.
    ASSERT_EQ(expected_containers.size(), output_.NumPairs());
    for (const string &container_name : expected_containers) {
      EXPECT_TRUE(output_.ContainsPair("name", container_name))
          << "Expected to find container " << container_name;
    }
  }

  // Expect the output to contain the specified PIDs/TIDs. They should be under
  // the specified key_name in the output maps.
  void ExpectPids(const string &key_name,
                  const vector<pid_t> &expected_pids) {
    // Ensure all PIDs/TIDs are in the output.
    ASSERT_EQ(expected_pids.size(), output_.NumPairs());
    for (pid_t pid : expected_pids) {
      const string pid_as_str = Substitute("$0", pid);
      EXPECT_TRUE(output_.ContainsPair(key_name, pid_as_str))
          << "Expected to find PID/TID " << pid_as_str;
    }
  }

 protected:
  const vector<string> args_;
  unique_ptr<MockContainerApi> mock_lmctfy_;
  MockContainer *mock_container_;
  OutputMap output_;
};

// Tests for: list containers.

TEST_F(ListTest, ListContainersSuccessSelf) {
  const vector<string> argv = {"containers"};

  EXPECT_CALL(*mock_lmctfy_, Detect(Ge(0)))
      .WillRepeatedly(Return(string(kContainerName)));
  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillRepeatedly(Return(mock_container_));

  const vector<Container *> subcontainers = {
    new StrictMockContainer(kSubName1),
    new StrictMockContainer(kSubName2)
  };
  EXPECT_CALL(*mock_container_, ListSubcontainers(Container::LIST_SELF))
      .WillRepeatedly(Return(subcontainers));

  FLAGS_lmctfy_recursive = false;
  EXPECT_TRUE(ListContainers(argv, mock_lmctfy_.get(), &output_).ok());
  ExpectContainers(vector<string>({kSubName1, kSubName2}));
}

TEST_F(ListTest, ListContainersSelfDetectFails) {
  const vector<string> argv = {"containers"};

  EXPECT_CALL(*mock_lmctfy_, Detect(Ge(0)))
      .WillRepeatedly(Return(Status::CANCELLED));

  FLAGS_lmctfy_recursive = false;
  EXPECT_EQ(Status::CANCELLED,
            ListContainers(argv, mock_lmctfy_.get(), &output_));
}

TEST_F(ListTest, ListContainersSuccess) {
  const vector<string> argv = {"containers", kContainerName};

  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillRepeatedly(Return(mock_container_));

  const vector<Container *> subcontainers = {
    new StrictMockContainer(kSubName1),
    new StrictMockContainer(kSubName2)
  };
  EXPECT_CALL(*mock_container_, ListSubcontainers(Container::LIST_SELF))
      .WillRepeatedly(Return(subcontainers));

  FLAGS_lmctfy_recursive = false;
  EXPECT_TRUE(ListContainers(argv, mock_lmctfy_.get(),
                             &output_).ok());
  ExpectContainers(vector<string>({kSubName1, kSubName2}));
}

TEST_F(ListTest, ListContainersSuccessRecursive) {
  const vector<string> argv = {"containers", kContainerName};

  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillRepeatedly(Return(mock_container_));

  const vector<Container *> subcontainers = {
    new StrictMockContainer(kSubName1),
    new StrictMockContainer(kSubName2)
  };
  EXPECT_CALL(*mock_container_, ListSubcontainers(Container::LIST_RECURSIVE))
      .WillRepeatedly(Return(subcontainers));

  FLAGS_lmctfy_recursive = true;
  EXPECT_TRUE(ListContainers(argv, mock_lmctfy_.get(), &output_).ok());
  ExpectContainers(vector<string>({kSubName1, kSubName2}));
}

TEST_F(ListTest, ListContainersListFails) {
  const vector<string> argv = {"containers", kContainerName};

  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillRepeatedly(Return(mock_container_));

  const vector<Container *> subcontainers = {
    new StrictMockContainer(kSubName1),
    new StrictMockContainer(kSubName2)
  };
  EXPECT_CALL(*mock_container_, ListSubcontainers(Container::LIST_SELF))
      .WillRepeatedly(Return(Status::CANCELLED));

  FLAGS_lmctfy_recursive = false;
  EXPECT_EQ(Status::CANCELLED,
            ListContainers(argv, mock_lmctfy_.get(), &output_));
}

TEST_F(ListTest, ListContainersGetContainerFails) {
  const vector<string> argv = {"containers", kContainerName};

  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillRepeatedly(Return(Status::CANCELLED));

  FLAGS_lmctfy_recursive = false;
  EXPECT_EQ(Status::CANCELLED,
            ListContainers(argv, mock_lmctfy_.get(), &output_));

  // Since the container was not deleted, we have to delete it.
  delete mock_container_;
}

// Tests for: list pids.

TEST_F(ListTest, ListPidsSuccessSelf) {
  const vector<string> argv = {"pids"};

  EXPECT_CALL(*mock_lmctfy_, Detect(Ge(0)))
      .WillRepeatedly(Return(string(kContainerName)));
  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillRepeatedly(Return(mock_container_));

  const vector<pid_t> pids = {1, 2, 3};
  EXPECT_CALL(*mock_container_, ListProcesses(Container::LIST_SELF))
      .WillRepeatedly(Return(pids));

  FLAGS_lmctfy_recursive = false;
  EXPECT_TRUE(ListPids(argv, mock_lmctfy_.get(), &output_).ok());
  ExpectPids("pid", pids);
}

TEST_F(ListTest, ListPidsSelfDetectFails) {
  const vector<string> argv = {"pids"};

  EXPECT_CALL(*mock_lmctfy_, Detect(Ge(0)))
      .WillRepeatedly(Return(Status::CANCELLED));

  FLAGS_lmctfy_recursive = false;
  EXPECT_EQ(Status::CANCELLED,
            ListPids(argv, mock_lmctfy_.get(), &output_));
}

TEST_F(ListTest, ListPidsSuccess) {
  const vector<string> argv = {"pids", kContainerName};

  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillRepeatedly(Return(mock_container_));

  const vector<pid_t> pids = {1, 2, 3};
  EXPECT_CALL(*mock_container_, ListProcesses(Container::LIST_SELF))
      .WillRepeatedly(Return(pids));

  FLAGS_lmctfy_recursive = false;
  EXPECT_TRUE(ListPids(argv, mock_lmctfy_.get(), &output_).ok());
  ExpectPids("pid", pids);
}

TEST_F(ListTest, ListPidsSuccessRecursive) {
  const vector<string> argv = {"pids", kContainerName};

  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillRepeatedly(Return(mock_container_));

  const vector<pid_t> pids = {1, 2, 3};
  EXPECT_CALL(*mock_container_, ListProcesses(Container::LIST_RECURSIVE))
      .WillRepeatedly(Return(pids));

  FLAGS_lmctfy_recursive = true;
  EXPECT_TRUE(ListPids(argv, mock_lmctfy_.get(), &output_).ok());
  ExpectPids("pid", pids);
}

TEST_F(ListTest, ListPidsListFails) {
  const vector<string> argv = {"pids", kContainerName};

  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillRepeatedly(Return(mock_container_));

  const vector<pid_t> pids = {1, 2, 3};
  EXPECT_CALL(*mock_container_, ListProcesses(Container::LIST_SELF))
      .WillRepeatedly(Return(Status::CANCELLED));

  FLAGS_lmctfy_recursive = false;
  EXPECT_EQ(Status::CANCELLED, ListPids(argv, mock_lmctfy_.get(), &output_));
}

TEST_F(ListTest, ListPidsGetContainerFails) {
  const vector<string> argv = {"pids", kContainerName};

  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillRepeatedly(Return(Status::CANCELLED));

  FLAGS_lmctfy_recursive = false;
  EXPECT_EQ(Status::CANCELLED, ListPids(argv, mock_lmctfy_.get(), &output_));

  // Since the container was not deleted, we have to delete it.
  delete mock_container_;
}

// Tests for: list tids.

TEST_F(ListTest, ListTidsSuccessSelf) {
  const vector<string> argv = {"tids"};

  EXPECT_CALL(*mock_lmctfy_, Detect(Ge(0)))
      .WillRepeatedly(Return(string(kContainerName)));
  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillRepeatedly(Return(mock_container_));

  const vector<pid_t> tids = {1, 2, 3};
  EXPECT_CALL(*mock_container_, ListThreads(Container::LIST_SELF))
      .WillRepeatedly(Return(tids));

  FLAGS_lmctfy_recursive = false;
  EXPECT_TRUE(ListTids(argv, mock_lmctfy_.get(), &output_).ok());
  ExpectPids("tid", tids);
}

TEST_F(ListTest, ListTidsSelfDetectFails) {
  const vector<string> argv = {"tids"};

  EXPECT_CALL(*mock_lmctfy_, Detect(Ge(0)))
      .WillRepeatedly(Return(Status::CANCELLED));

  FLAGS_lmctfy_recursive = false;
  EXPECT_EQ(Status::CANCELLED,
            ListTids(argv, mock_lmctfy_.get(), &output_));
}

TEST_F(ListTest, ListTidsSuccess) {
  const vector<string> argv = {"tids", kContainerName};

  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillRepeatedly(Return(mock_container_));

  const vector<pid_t> tids = {1, 2, 3};
  EXPECT_CALL(*mock_container_, ListThreads(Container::LIST_SELF))
      .WillRepeatedly(Return(tids));

  FLAGS_lmctfy_recursive = false;
  EXPECT_TRUE(ListTids(argv, mock_lmctfy_.get(), &output_).ok());
  ExpectPids("tid", tids);
}

TEST_F(ListTest, ListTidsSuccessRecursive) {
  const vector<string> argv = {"tids", kContainerName};

  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillRepeatedly(Return(mock_container_));

  const vector<pid_t> tids = {1, 2, 3};
  EXPECT_CALL(*mock_container_, ListThreads(Container::LIST_RECURSIVE))
      .WillRepeatedly(Return(tids));

  FLAGS_lmctfy_recursive = true;
  EXPECT_TRUE(ListTids(argv, mock_lmctfy_.get(), &output_).ok());
  ExpectPids("tid", tids);
}

TEST_F(ListTest, ListTidsListFails) {
  const vector<string> argv = {"tids", kContainerName};

  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillRepeatedly(Return(mock_container_));

  const vector<pid_t> tids = {1, 2, 3};
  EXPECT_CALL(*mock_container_, ListThreads(Container::LIST_SELF))
      .WillRepeatedly(Return(Status::CANCELLED));

  FLAGS_lmctfy_recursive = false;
  EXPECT_EQ(Status::CANCELLED, ListTids(argv, mock_lmctfy_.get(), &output_));
}

TEST_F(ListTest, ListTidsGetContainerFails) {
  const vector<string> argv = {"tids", kContainerName};

  EXPECT_CALL(*mock_lmctfy_, Get(kContainerName))
      .WillRepeatedly(Return(Status::CANCELLED));

  FLAGS_lmctfy_recursive = false;
  EXPECT_EQ(Status::CANCELLED, ListTids(argv, mock_lmctfy_.get(), &output_));

  // Since the container was not deleted, we have to delete it.
  delete mock_container_;
}

}  // namespace
}  // namespace cli
}  // namespace lmctfy
}  // namespace containers
