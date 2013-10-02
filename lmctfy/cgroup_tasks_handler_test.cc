// Copyright 2013 Google Inc. All Rights Reserved.
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
#include "strings/stringpiece.h"
#include "strings/substitute.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "util/task/codes.pb.h"

using ::system_api::KernelAPIMock;
using ::file::JoinPath;
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
  virtual void SetUp() {
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
  EXPECT_CALL(*mock_cgroup_controller_factory_, Create(kContainer))
      .WillOnce(Return(new StrictMockJobController()));

  StatusOr<TasksHandler *> statusor = factory_->Create(kContainer);
  ASSERT_TRUE(statusor.ok());
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  delete statusor.ValueOrDie();
}

TEST_F(CgroupTasksHandlerFactoryTest, CreateFails) {
  EXPECT_CALL(*mock_cgroup_controller_factory_, Create(kContainer))
      .WillOnce(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED, factory_->Create(kContainer).status());
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

TEST_F(CgroupTasksHandlerFactoryTest, DetectSuccess) {
  const string kProcCgroup =
      "7:net:/sys\n"
      "6:memory:/sys\n"
      "5:job:/sys/subcont\n"
      "4:io:/sys\n"
      "3:cpuset:/sys\n"
      "2:cpuacct,cpu:/sys\n"
      "1:bcache,rlimit,perf_event:/sys\n";

  EXPECT_CALL(*mock_kernel_, ReadFileToString("/proc/self/cgroup", NotNull()))
      .WillRepeatedly(DoAll(SetArgPointee<1>(kProcCgroup), Return(true)));
  EXPECT_CALL(*mock_cgroup_controller_factory_, HierarchyName())
      .WillRepeatedly(Return(string("job")));

  StatusOr<string> statusor = factory_->Detect(0);
  ASSERT_TRUE(statusor.ok());
  EXPECT_EQ("/sys/subcont", statusor.ValueOrDie());
}

TEST_F(CgroupTasksHandlerFactoryTest, DetectProcCgroupIsEmpty) {
  const string kProcCgroup = "";

  EXPECT_CALL(*mock_kernel_, ReadFileToString("/proc/self/cgroup", NotNull()))
      .WillRepeatedly(DoAll(SetArgPointee<1>(kProcCgroup), Return(true)));
  EXPECT_CALL(*mock_cgroup_controller_factory_, HierarchyName())
      .WillRepeatedly(Return(string("job")));

  StatusOr<string> statusor = factory_->Detect(0);
  EXPECT_FALSE(statusor.ok());
  EXPECT_EQ(::util::error::NOT_FOUND, statusor.status().error_code());
}

TEST_F(CgroupTasksHandlerFactoryTest, DetectWithTidSuccess) {
  const string kProcCgroup =
      "7:net:/sys\n"
      "6:memory:/sys\n"
      "5:job:/sys/subcont\n"
      "4:io:/sys\n"
      "3:cpuset:/sys\n"
      "2:cpuacct,cpu:/sys\n"
      "1:bcache,rlimit,perf_event:/sys\n";

  EXPECT_CALL(*mock_kernel_, ReadFileToString("/proc/12/cgroup", NotNull()))
      .WillRepeatedly(DoAll(SetArgPointee<1>(kProcCgroup), Return(true)));
  EXPECT_CALL(*mock_cgroup_controller_factory_, HierarchyName())
      .WillRepeatedly(Return(string("job")));

  StatusOr<string> statusor = factory_->Detect(12);
  ASSERT_TRUE(statusor.ok());
  EXPECT_EQ("/sys/subcont", statusor.ValueOrDie());
}

TEST_F(CgroupTasksHandlerFactoryTest, DetectReadFileToStringFails) {
  EXPECT_CALL(*mock_kernel_, ReadFileToString("/proc/self/cgroup", NotNull()))
      .WillRepeatedly(Return(false));

  StatusOr<string> statusor = factory_->Detect(0);
  EXPECT_FALSE(statusor.ok());
  EXPECT_EQ(::util::error::FAILED_PRECONDITION, statusor.status().error_code());
}

TEST_F(CgroupTasksHandlerFactoryTest, DetectLineHasBadFormat) {
  const string kProcCgroup =
      "7:net:/sys\n"
      "6memory/sys\n"
      "5:job:/sys/subcont\n"
      "4:io:/sys\n"
      "3:cpuset:/sys\n"
      "2:cpuacct,cpu:/sys\n"
      "1:bcache,rlimit,perf_event:/sys\n";

  EXPECT_CALL(*mock_kernel_, ReadFileToString("/proc/12/cgroup", NotNull()))
      .WillRepeatedly(DoAll(SetArgPointee<1>(kProcCgroup), Return(true)));
  EXPECT_CALL(*mock_cgroup_controller_factory_, HierarchyName())
      .WillRepeatedly(Return(string("job")));

  StatusOr<string> statusor = factory_->Detect(12);
  ASSERT_TRUE(statusor.ok());
  EXPECT_EQ("/sys/subcont", statusor.ValueOrDie());
}

TEST_F(CgroupTasksHandlerFactoryTest, DetectLineHasMoreElementsThanExpected) {
  const string kProcCgroup =
      "7:net:/sys:new\n"
      "6:memory:/sys:new\n"
      "5:job:/sys/subcont:new\n"
      "4:io:/sys:new\n"
      "3:cpuset:/sys:new\n"
      "2:cpuacct,cpu:/sys:new\n"
      "1:bcache,rlimit,perf_event:/sys:new\n";

  EXPECT_CALL(*mock_kernel_, ReadFileToString("/proc/12/cgroup", NotNull()))
      .WillRepeatedly(DoAll(SetArgPointee<1>(kProcCgroup), Return(true)));
  EXPECT_CALL(*mock_cgroup_controller_factory_, HierarchyName())
      .WillRepeatedly(Return(string("job")));

  StatusOr<string> statusor = factory_->Detect(12);
  ASSERT_TRUE(statusor.ok());
  EXPECT_EQ("/sys/subcont", statusor.ValueOrDie());
}

TEST_F(CgroupTasksHandlerFactoryTest, DetectComountedSubsystems) {
  const string kProcCgroup =
      "6:net:/sys\n"
      "5:memory:/sys\n"
      "4:cpuacct,job,cpu:/sys/subcont\n"
      "3:io:/sys\n"
      "2:cpuset:/sys\n"
      "1:bcache,rlimit,perf_event:/sys\n";

  EXPECT_CALL(*mock_kernel_, ReadFileToString("/proc/12/cgroup", NotNull()))
      .WillRepeatedly(DoAll(SetArgPointee<1>(kProcCgroup), Return(true)));
  EXPECT_CALL(*mock_cgroup_controller_factory_, HierarchyName())
      .WillRepeatedly(Return(string("job")));

  StatusOr<string> statusor = factory_->Detect(12);
  ASSERT_TRUE(statusor.ok());
  EXPECT_EQ("/sys/subcont", statusor.ValueOrDie());
}

TEST_F(CgroupTasksHandlerFactoryTest, DetectSubsystemNotFound) {
  const string kProcCgroup =
      "6:net:/sys\n"
      "5:memory:/sys\n"
      "4:io:/sys\n"
      "3:cpuset:/sys\n"
      "2:cpuacct,cpu:/sys\n"
      "1:bcache,rlimit,perf_event:/sys\n";

  EXPECT_CALL(*mock_kernel_, ReadFileToString("/proc/12/cgroup", NotNull()))
      .WillRepeatedly(DoAll(SetArgPointee<1>(kProcCgroup), Return(true)));
  EXPECT_CALL(*mock_cgroup_controller_factory_, HierarchyName())
      .WillRepeatedly(Return(string("job")));

  StatusOr<string> statusor = factory_->Detect(12);
  EXPECT_FALSE(statusor.ok());
  EXPECT_EQ(::util::error::NOT_FOUND, statusor.status().error_code());
}

class CgroupTasksHandlerTest : public ::testing::Test {
 public:
  CgroupTasksHandlerTest() {}

  virtual void SetUp() {
    mock_cgroup_controller_ =
        new StrictMockCgroupController(CGROUP_JOB, kContainerCgroupPath, false);
    handler_.reset(new CgroupTasksHandler(kContainer, mock_cgroup_controller_));
  }

  void CheckPids(const vector<pid_t> &expected, const vector<pid_t> &result) {
    EXPECT_THAT(expected.size(), result.size());
    for (const auto &pid : expected) {
      EXPECT_THAT(result, Contains(pid));
    }
  }

 protected:
  MockCgroupController *mock_cgroup_controller_;

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

TEST_F(CgroupTasksHandlerTest, ListProcessesSuccess) {
  const vector<pid_t> expected = {11, 12, 13};

  EXPECT_CALL(*mock_cgroup_controller_, GetProcesses())
      .WillRepeatedly(Return(expected));

  StatusOr<vector<pid_t>> statusor = handler_->ListProcesses();
  EXPECT_TRUE(statusor.ok());
  CheckPids(expected, statusor.ValueOrDie());
}

TEST_F(CgroupTasksHandlerTest, ListThreadsSuccess) {
  const vector<pid_t> expected = {11, 12, 13};

  EXPECT_CALL(*mock_cgroup_controller_, GetThreads())
      .WillRepeatedly(Return(expected));

  StatusOr<vector<pid_t>> statusor = handler_->ListThreads();
  EXPECT_TRUE(statusor.ok());
  CheckPids(expected, statusor.ValueOrDie());
}

TEST_F(CgroupTasksHandlerTest, ListProcessesEmpty) {
  EXPECT_CALL(*mock_cgroup_controller_, GetProcesses())
      .WillRepeatedly(Return(vector<pid_t>()));

  StatusOr<vector<pid_t>> statusor = handler_->ListProcesses();
  EXPECT_TRUE(statusor.ok());
  EXPECT_EQ(0, statusor.ValueOrDie().size());
}

TEST_F(CgroupTasksHandlerTest, ListThreadsEmpty) {
  EXPECT_CALL(*mock_cgroup_controller_, GetThreads())
      .WillRepeatedly(Return(vector<pid_t>()));

  StatusOr<vector<pid_t>> statusor = handler_->ListThreads();
  EXPECT_TRUE(statusor.ok());
  EXPECT_EQ(0, statusor.ValueOrDie().size());
}

TEST_F(CgroupTasksHandlerTest, ListSubcontainersSuccess) {
  const vector<string> expected = {"sub1", "sub2", "sub3"};

  EXPECT_CALL(*mock_cgroup_controller_, GetSubcontainers())
      .WillRepeatedly(Return(expected));

  StatusOr<vector<string>> statusor = handler_->ListSubcontainers();
  ASSERT_TRUE(statusor.ok());
  EXPECT_EQ(3, statusor.ValueOrDie().size());

  vector<string> subcontainers = statusor.ValueOrDie();
  EXPECT_THAT(subcontainers, Contains(JoinPath(kContainer, "sub1")));
  EXPECT_THAT(subcontainers, Contains(JoinPath(kContainer, "sub2")));
  EXPECT_THAT(subcontainers, Contains(JoinPath(kContainer, "sub3")));
}

TEST_F(CgroupTasksHandlerTest, ListSubcontainersNoSubcontainers) {
  EXPECT_CALL(*mock_cgroup_controller_, GetSubcontainers())
      .WillRepeatedly(Return(vector<string>()));

  StatusOr<vector<string>> statusor = handler_->ListSubcontainers();
  ASSERT_TRUE(statusor.ok());
  EXPECT_EQ(0, statusor.ValueOrDie().size());
}

TEST_F(CgroupTasksHandlerTest, ListSubcontainersFails) {
  EXPECT_CALL(*mock_cgroup_controller_, GetSubcontainers())
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED, handler_->ListSubcontainers().status());
}

}  // namespace
}  // namespace lmctfy
}  // namespace containers
