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

#include "lmctfy/cgroup_tasks_handler.h"

#include <sys/stat.h>
#include <unistd.h>
#include <memory>
#include <vector>

#include "base/logging.h"
#include "system_api/kernel_api_mock.h"
#include "file/base/path.h"
#include "lmctfy/controllers/cgroup_controller_mock.h"
#include "lmctfy/controllers/cgroup_factory_mock.h"
#include "lmctfy/controllers/job_controller_mock.h"
#include "lmctfy/tasks_handler_mock.h"
#include "util/safe_types/unix_gid.h"
#include "util/safe_types/unix_uid.h"
#include "util/errors_test_util.h"
#include "strings/stringpiece.h"
#include "strings/substitute.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "util/task/codes.pb.h"

using ::system_api::KernelAPIMock;
using ::file::JoinPath;
using ::util::UnixGid;
using ::util::UnixUid;
using ::std::unique_ptr;
using ::strings::SubstituteAndAppend;
using ::testing::Contains;
using ::testing::DoAll;
using ::testing::NotNull;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::StrictMock;
using ::std::unique_ptr;
using ::std::vector;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace lmctfy {
namespace {

static const char kMountPath[] = "/dev/cgroup/job";
static const char kContainer[] = "/test";
static const char kContainerCgroupPath[] = "/dev/cgroup/job/test";

class CgroupTasksHandlerFactoryTest : public ::testing::Test {
 public:
  void SetUp() override {
    mock_kernel_.reset(new StrictMock<KernelAPIMock>());
    unique_ptr<MockCgroupFactory> mock_cgroup_factory(
        new NiceMockCgroupFactory());
    mock_cgroup_controller_factory_ =
        new StrictMockJobControllerFactory(mock_cgroup_factory.get());
    factory_.reset(new CgroupTasksHandlerFactory<JobController>(
        mock_cgroup_controller_factory_, mock_kernel_.get()));
  }

 protected:
  MockJobControllerFactory *mock_cgroup_controller_factory_;

  unique_ptr<KernelAPIMock> mock_kernel_;
  unique_ptr<CgroupTasksHandlerFactory<JobController>> factory_;
};

TEST_F(CgroupTasksHandlerFactoryTest, CreateSuccess) {
  ContainerSpec spec;

  EXPECT_CALL(*mock_cgroup_controller_factory_, Create(kContainer))
      .WillOnce(Return(new StrictMockJobController()));

  StatusOr<TasksHandler *> statusor = factory_->Create(kContainer, spec);
  ASSERT_TRUE(statusor.ok());
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  delete statusor.ValueOrDie();
}

TEST_F(CgroupTasksHandlerFactoryTest, CreateFails) {
  ContainerSpec spec;

  EXPECT_CALL(*mock_cgroup_controller_factory_, Create(kContainer))
      .WillOnce(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED, factory_->Create(kContainer, spec).status());
}

TEST_F(CgroupTasksHandlerFactoryTest, GetSuccess) {
  EXPECT_CALL(*mock_cgroup_controller_factory_, Get(kContainer))
      .WillOnce(Return(new StrictMockJobController()));

  StatusOr<TasksHandler *> statusor = factory_->Get(kContainer);
  ASSERT_TRUE(statusor.ok());
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  delete statusor.ValueOrDie();
}

TEST_F(CgroupTasksHandlerFactoryTest, GetFails) {
  EXPECT_CALL(*mock_cgroup_controller_factory_, Get(kContainer))
      .WillOnce(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED, factory_->Get(kContainer).status());
}

TEST_F(CgroupTasksHandlerFactoryTest, Exists) {
  // Exists
  EXPECT_CALL(*mock_cgroup_controller_factory_, Exists(kContainer))
      .WillRepeatedly(Return(true));

  EXPECT_TRUE(factory_->Exists(kContainer));

  // Does not exist
  EXPECT_CALL(*mock_cgroup_controller_factory_, Exists(kContainer))
      .WillRepeatedly(Return(false));

  EXPECT_FALSE(factory_->Exists(kContainer));
}

// Tests for Detect().

TEST_F(CgroupTasksHandlerFactoryTest, DetectSuccess) {
  const pid_t kTid = 0;

  EXPECT_CALL(*mock_cgroup_controller_factory_, DetectCgroupPath(kTid))
      .WillRepeatedly(Return(string(kContainer)));

  StatusOr<string> statusor = factory_->Detect(kTid);
  ASSERT_OK(statusor);
  EXPECT_EQ(kContainer, statusor.ValueOrDie());
}

TEST_F(CgroupTasksHandlerFactoryTest, DetectFails) {
  const pid_t kTid = 0;

  EXPECT_CALL(*mock_cgroup_controller_factory_, DetectCgroupPath(kTid))
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED, factory_->Detect(kTid).status());
}

class CgroupTasksHandlerTest : public ::testing::Test {
 public:
  void SetUp() override {
    mock_cgroup_controller_ =
        new StrictMockCgroupController(CGROUP_JOB, kContainerCgroupPath, false);
    mock_tasks_handler_factory_.reset(new StrictMockTasksHandlerFactory());
    handler_.reset(new CgroupTasksHandler(kContainer, mock_cgroup_controller_,
                                          mock_tasks_handler_factory_.get()));
  }

  void CheckPids(const vector<pid_t> &expected, const vector<pid_t> &result) {
    EXPECT_EQ(expected.size(), result.size());
    for (const auto &pid : expected) {
      EXPECT_THAT(result, Contains(pid));
    }
  }

  // Expect to Get() a TasksHandler. This is not owned by the caller.
  MockTasksHandler *ExpectGetTasksHandler(const string &container_name) {
    MockTasksHandler *handler = new StrictMockTasksHandler(container_name);
    EXPECT_CALL(*mock_tasks_handler_factory_, Get(container_name))
        .WillOnce(Return(handler));
    return handler;
  }

 protected:
  MockCgroupController *mock_cgroup_controller_;
  unique_ptr<MockTasksHandlerFactory> mock_tasks_handler_factory_;
  unique_ptr<CgroupTasksHandler> handler_;
};

TEST_F(CgroupTasksHandlerTest, DestroySuccess) {
  EXPECT_CALL(*mock_cgroup_controller_, Destroy())
      .WillOnce(Return(Status::OK));

  // Deletes itself on success
  EXPECT_TRUE(handler_.release()->Destroy().ok());
  delete mock_cgroup_controller_;
}

TEST_F(CgroupTasksHandlerTest, DestroyFails) {
  EXPECT_CALL(*mock_cgroup_controller_, Destroy())
      .WillRepeatedly(Return(Status::CANCELLED));

  // Deletes itself on success
  EXPECT_EQ(Status::CANCELLED, handler_->Destroy());
}

// Tests for TrackTasks().

TEST_F(CgroupTasksHandlerTest, TrackTasksSuccess) {
  EXPECT_CALL(*mock_cgroup_controller_, Enter(12))
      .WillOnce(Return(Status::OK));

  vector<pid_t> tids;
  tids.push_back(12);
  EXPECT_TRUE(handler_->TrackTasks(tids).ok());
}

TEST_F(CgroupTasksHandlerTest, TrackTasksMultipleTids) {
  EXPECT_CALL(*mock_cgroup_controller_, Enter(12))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_cgroup_controller_, Enter(13))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_cgroup_controller_, Enter(14))
      .WillOnce(Return(Status::OK));

  vector<pid_t> tids;
  tids.push_back(12);
  tids.push_back(13);
  tids.push_back(14);
  EXPECT_TRUE(handler_->TrackTasks(tids).ok());
}

TEST_F(CgroupTasksHandlerTest, TrackTasksSelf) {
  EXPECT_CALL(*mock_cgroup_controller_, Enter(0))
      .WillOnce(Return(Status::OK));

  vector<pid_t> tids;
  tids.push_back(0);
  EXPECT_TRUE(handler_->TrackTasks(tids).ok());
}

TEST_F(CgroupTasksHandlerTest, TrackTasksFails) {
  EXPECT_CALL(*mock_cgroup_controller_, Enter(11))
      .WillOnce(Return(Status::CANCELLED));

  vector<pid_t> tids;
  tids.push_back(11);
  EXPECT_EQ(Status::CANCELLED, handler_->TrackTasks(tids));
}

// Tests for Delegate().

TEST_F(CgroupTasksHandlerTest, DelegateSuccess) {
  const UnixUid kUid(2);
  const UnixGid kGid(3);

  EXPECT_CALL(*mock_cgroup_controller_, Delegate(kUid, kGid))
      .WillOnce(Return(Status::OK));

  EXPECT_OK(handler_->Delegate(kUid, kGid));
}

TEST_F(CgroupTasksHandlerTest, DelegateFails) {
  const UnixUid kUid(2);
  const UnixGid kGid(3);

  EXPECT_CALL(*mock_cgroup_controller_, Delegate(kUid, kGid))
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED, handler_->Delegate(kUid, kGid));
}

// Tests for ListSubcontainers().

TEST_F(CgroupTasksHandlerTest, ListSubcontainersSelf_Success) {
  const vector<string> kExpected = {"sub1", "sub2", "sub3"};

  EXPECT_CALL(*mock_cgroup_controller_, GetSubcontainers())
      .WillRepeatedly(Return(kExpected));

  StatusOr<vector<string>> statusor =
      handler_->ListSubcontainers(TasksHandler::ListType::SELF);
  ASSERT_OK(statusor);

  const vector<string> subcontainers = statusor.ValueOrDie();
  EXPECT_EQ(3, subcontainers.size());
  EXPECT_THAT(subcontainers, Contains(JoinPath(kContainer, "sub1")));
  EXPECT_THAT(subcontainers, Contains(JoinPath(kContainer, "sub2")));
  EXPECT_THAT(subcontainers, Contains(JoinPath(kContainer, "sub3")));
}

TEST_F(CgroupTasksHandlerTest, ListSubcontainersSelf_NoSubcontainers) {
  EXPECT_CALL(*mock_cgroup_controller_, GetSubcontainers())
      .WillRepeatedly(Return(vector<string>()));

  StatusOr<vector<string>> statusor =
      handler_->ListSubcontainers(TasksHandler::ListType::SELF);
  ASSERT_OK(statusor);
  EXPECT_EQ(0, statusor.ValueOrDie().size());
}

TEST_F(CgroupTasksHandlerTest, ListSubcontainersSelf_Fails) {
  EXPECT_CALL(*mock_cgroup_controller_, GetSubcontainers())
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED,
            handler_->ListSubcontainers(TasksHandler::ListType::SELF).status());
}

TEST_F(CgroupTasksHandlerTest, ListSubcontainersRecursive_Success) {
  const string kContainer1 = JoinPath(kContainer, "sub1");
  const string kContainer2 = JoinPath(kContainer, "sub2");
  const string kContainer3 = JoinPath(kContainer, "sub1", "ssub1");
  MockTasksHandler *mock_handler1 = ExpectGetTasksHandler(kContainer1);
  MockTasksHandler *mock_handler2 = ExpectGetTasksHandler(kContainer2);
  MockTasksHandler *mock_handler3 = ExpectGetTasksHandler(kContainer3);

  EXPECT_CALL(*mock_cgroup_controller_, GetSubcontainers())
      .WillRepeatedly(Return(vector<string>{"sub1", "sub2"}));

  EXPECT_CALL(*mock_handler1, ListSubcontainers(TasksHandler::ListType::SELF))
      .WillRepeatedly(Return(vector<string>{kContainer3}));
  EXPECT_CALL(*mock_handler2, ListSubcontainers(TasksHandler::ListType::SELF))
      .WillRepeatedly(Return(vector<string>()));
  EXPECT_CALL(*mock_handler3, ListSubcontainers(TasksHandler::ListType::SELF))
      .WillRepeatedly(Return(vector<string>()));

  StatusOr<vector<string>> statusor =
      handler_->ListSubcontainers(TasksHandler::ListType::RECURSIVE);
  ASSERT_OK(statusor);

  const vector<string> subcontainers = statusor.ValueOrDie();
  EXPECT_EQ(3, subcontainers.size());
  EXPECT_THAT(subcontainers, Contains(kContainer1));
  EXPECT_THAT(subcontainers, Contains(kContainer2));
  EXPECT_THAT(subcontainers, Contains(kContainer3));
}

TEST_F(CgroupTasksHandlerTest, ListSubcontainersRecursive_Fails) {
  const string kContainer1 = JoinPath(kContainer, "sub1");
  const string kContainer2 = JoinPath(kContainer, "sub2");
  const string kContainer3 = JoinPath(kContainer, "sub1", "ssub1");
  MockTasksHandler *mock_handler1 = ExpectGetTasksHandler(kContainer1);
  MockTasksHandler *mock_handler2 = ExpectGetTasksHandler(kContainer2);
  MockTasksHandler *mock_handler3 = ExpectGetTasksHandler(kContainer3);

  EXPECT_CALL(*mock_cgroup_controller_, GetSubcontainers())
      .WillRepeatedly(Return(vector<string>{"sub1", "sub2"}));

  EXPECT_CALL(*mock_handler1, ListSubcontainers(TasksHandler::ListType::SELF))
      .WillRepeatedly(Return(vector<string>{kContainer3}));
  EXPECT_CALL(*mock_handler2, ListSubcontainers(TasksHandler::ListType::SELF))
      .WillRepeatedly(Return(vector<string>()));
  EXPECT_CALL(*mock_handler3, ListSubcontainers(TasksHandler::ListType::SELF))
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_EQ(
      Status::CANCELLED,
      handler_->ListSubcontainers(TasksHandler::ListType::RECURSIVE).status());
}

// TODO(vmarmol): Consider using a DirectoryLister element rather than using a
// partial mock.
// A partial mock of CgroupTasksHandler that only mocks ListSubcontainers. Used
// for testing ListProcesses() and ListThreads().
class PartialMockCgroupTasksHandler : public CgroupTasksHandler {
 public:
  PartialMockCgroupTasksHandler(
      const string &container_name, CgroupController *cgroup_controller,
      const TasksHandlerFactory *tasks_handler_factory)
      : CgroupTasksHandler(container_name, cgroup_controller,
                           tasks_handler_factory) {}

  MOCK_CONST_METHOD1(ListSubcontainers,
                     StatusOr<vector<string>>(TasksHandler::ListType type));
};

class CgroupTasksHandlerPidsTidsTest : public CgroupTasksHandlerTest {
 public:
  void SetUp() override {
    CgroupTasksHandlerTest::SetUp();

    mock_cgroup_controller_ =
        new StrictMockCgroupController(CGROUP_JOB, kContainerCgroupPath, false);
    partial_handler_.reset(
        new PartialMockCgroupTasksHandler(kContainer, mock_cgroup_controller_,
                                          mock_tasks_handler_factory_.get()));
  }

 protected:
  unique_ptr<PartialMockCgroupTasksHandler> partial_handler_;
};

// Tests for ListProcesses().

TEST_F(CgroupTasksHandlerPidsTidsTest, ListProcessesSelf_Success) {
  const vector<pid_t> kExpected = {11, 12, 13};

  EXPECT_CALL(*mock_cgroup_controller_, GetThreads())
      .WillRepeatedly(Return(kExpected));
  EXPECT_CALL(*mock_cgroup_controller_, GetProcesses())
      .WillRepeatedly(Return(kExpected));

  StatusOr<vector<pid_t>> statusor =
      partial_handler_->ListProcesses(TasksHandler::ListType::SELF);
  ASSERT_OK(statusor);
  CheckPids(kExpected, statusor.ValueOrDie());
}

TEST_F(CgroupTasksHandlerPidsTidsTest, ListProcessesRecursive_Success) {
  EXPECT_CALL(*mock_cgroup_controller_, GetProcesses())
      .WillRepeatedly(Return(vector<pid_t>{1, 2}));

  // Expect 2 containers with 2 threads each.
  const string sub1 = JoinPath(kContainer, "s1");
  const string sub2 = JoinPath(kContainer, "s2");
  EXPECT_CALL(*partial_handler_,
              ListSubcontainers(TasksHandler::ListType::RECURSIVE))
      .WillRepeatedly(Return(vector<string>{sub1, sub2}));
  MockTasksHandler *handler1 = ExpectGetTasksHandler(sub1);
  MockTasksHandler *handler2 = ExpectGetTasksHandler(sub2);
  EXPECT_CALL(*handler1, ListProcesses(TasksHandler::ListType::SELF))
      .WillRepeatedly(Return(vector<pid_t>{3, 4}));
  EXPECT_CALL(*handler2, ListProcesses(TasksHandler::ListType::SELF))
      .WillRepeatedly(Return(vector<pid_t>{5, 6}));

  StatusOr<vector<pid_t>> statusor =
      partial_handler_->ListProcesses(TasksHandler::ListType::RECURSIVE);
  ASSERT_OK(statusor);
  CheckPids({1, 2, 3, 4, 5, 6}, statusor.ValueOrDie());
}

TEST_F(CgroupTasksHandlerPidsTidsTest, ListProcessesSelf_Empty) {
  EXPECT_CALL(*mock_cgroup_controller_, GetThreads())
      .WillRepeatedly(Return(vector<pid_t>()));
  EXPECT_CALL(*mock_cgroup_controller_, GetProcesses())
      .WillRepeatedly(Return(vector<pid_t>()));

  StatusOr<vector<pid_t>> statusor =
      partial_handler_->ListProcesses(TasksHandler::ListType::SELF);
  ASSERT_OK(statusor);
  EXPECT_EQ(0, statusor.ValueOrDie().size());
}

// Tests for ListThreads().

TEST_F(CgroupTasksHandlerPidsTidsTest, ListThreadsSelf_Success) {
  const vector<pid_t> kExpected = {11, 12, 13};

  EXPECT_CALL(*mock_cgroup_controller_, GetThreads())
      .WillRepeatedly(Return(kExpected));

  StatusOr<vector<pid_t>> statusor =
      partial_handler_->ListThreads(TasksHandler::ListType::SELF);
  ASSERT_OK(statusor);
  CheckPids(kExpected, statusor.ValueOrDie());
}

TEST_F(CgroupTasksHandlerPidsTidsTest, ListThreadsRecursive_Success) {
  EXPECT_CALL(*mock_cgroup_controller_, GetThreads())
      .WillRepeatedly(Return(vector<pid_t>{1, 2}));

  // Expect 2 containers with 2 threads each.
  const string sub1 = JoinPath(kContainer, "s1");
  const string sub2 = JoinPath(kContainer, "s2");
  EXPECT_CALL(*partial_handler_,
              ListSubcontainers(TasksHandler::ListType::RECURSIVE))
      .WillRepeatedly(Return(vector<string>{sub1, sub2}));
  MockTasksHandler *handler1 = ExpectGetTasksHandler(sub1);
  MockTasksHandler *handler2 = ExpectGetTasksHandler(sub2);
  EXPECT_CALL(*handler1, ListThreads(TasksHandler::ListType::SELF))
      .WillRepeatedly(Return(vector<pid_t>{3, 4}));
  EXPECT_CALL(*handler2, ListThreads(TasksHandler::ListType::SELF))
      .WillRepeatedly(Return(vector<pid_t>{5, 6}));

  StatusOr<vector<pid_t>> statusor =
      partial_handler_->ListThreads(TasksHandler::ListType::RECURSIVE);
  ASSERT_OK(statusor);
  CheckPids({1, 2, 3, 4, 5, 6}, statusor.ValueOrDie());
}

TEST_F(CgroupTasksHandlerPidsTidsTest,
       ListThreadsRecursive_ListSubcontainersFails) {
  EXPECT_CALL(*mock_cgroup_controller_, GetThreads())
      .WillRepeatedly(Return(vector<pid_t>{1, 2}));

  const string sub1 = JoinPath(kContainer, "s1");
  const string sub2 = JoinPath(kContainer, "s2");
  EXPECT_CALL(*partial_handler_,
              ListSubcontainers(TasksHandler::ListType::RECURSIVE))
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED,
            partial_handler_->ListThreads(TasksHandler::ListType::RECURSIVE)
                .status());
}

TEST_F(CgroupTasksHandlerPidsTidsTest, ListThreadsRecursive_ListThreadsFails) {
  EXPECT_CALL(*mock_cgroup_controller_, GetThreads())
      .WillRepeatedly(Return(vector<pid_t>{1, 2}));

  const string sub1 = JoinPath(kContainer, "s1");
  const string sub2 = JoinPath(kContainer, "s2");
  EXPECT_CALL(*partial_handler_,
              ListSubcontainers(TasksHandler::ListType::RECURSIVE))
      .WillRepeatedly(Return(vector<string>{sub1, sub2}));
  MockTasksHandler *handler1 = ExpectGetTasksHandler(sub1);
  MockTasksHandler *handler2 = ExpectGetTasksHandler(sub2);
  EXPECT_CALL(*handler1, ListThreads(TasksHandler::ListType::SELF))
      .WillRepeatedly(Return(vector<pid_t>{3, 4}));
  EXPECT_CALL(*handler2, ListThreads(TasksHandler::ListType::SELF))
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED,
            partial_handler_->ListThreads(TasksHandler::ListType::RECURSIVE)
                .status());
}

TEST_F(CgroupTasksHandlerPidsTidsTest, ListThreadsSelf_Empty) {
  EXPECT_CALL(*mock_cgroup_controller_, GetThreads())
      .WillRepeatedly(Return(vector<pid_t>()));

  StatusOr<vector<pid_t>> statusor =
      partial_handler_->ListThreads(TasksHandler::ListType::SELF);
  ASSERT_OK(statusor);
  EXPECT_EQ(0, statusor.ValueOrDie().size());
}

}  // namespace
}  // namespace lmctfy
}  // namespace containers
