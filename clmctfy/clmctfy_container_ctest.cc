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

#include "clmctfy_container.h"

#include <vector>

#include "gtest/gtest.h"
#include "gmock/gmock.h"
#include "util/task/statusor.h"
#include "strings/stringpiece.h"

#include "lmctfy_mock.h"
#include "lmctfy.pb.h"
#include "clmctfy_macros_ctest.h"
#include "clmctfy_container_struct.h"
#include "clmctfy_container_api_struct.h"

using ::testing::NotNull;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::StrictMock;
using ::testing::_;
using ::util::Status;
using ::util::StatusOr;
using ::std::vector;

namespace containers {
namespace lmctfy {

StatusOr<ContainerApi *> ContainerApi::New() {
  return new StrictMockContainerApi();
}

Status ContainerApi::InitMachine(const InitSpec &spec) {
  return Status::OK;
}

class ClmctfyContainerTest : public ::testing::Test {
 public:
  virtual void SetUp() {
    container_api_ = NULL;
    container_ = NULL;
    const char *container_name = "/test";
    container_name_ = container_name;
    lmctfy_new_container_api(&container_api_, NULL);
    StrictMockContainerApi *mock_api = GetMockApi();
    Container *ctnr = new StrictMockContainer(container_name);
    StatusOr<Container *> statusor = StatusOr<Container *>(ctnr);
    EXPECT_CALL(*mock_api, Get(StringPiece(container_name))).WillOnce(Return(statusor));

    lmctfy_container_api_get_container(container_api_, container_name, &container_, NULL);
  }

  virtual void TearDown() {
    lmctfy_delete_container_api(container_api_);
    lmctfy_delete_container(container_);
  }
 protected:
  struct container_api *container_api_;
  struct container *container_;
  StrictMockContainerApi *GetMockApi();
  StrictMockContainer *GetMockContainer();
  string container_name_;
};

StrictMockContainerApi *ClmctfyContainerTest::GetMockApi() {
  ContainerApi *capi = container_api_->container_api_;
  StrictMockContainerApi *mock_api = dynamic_cast<StrictMockContainerApi *>(capi);
  return mock_api;
}

StrictMockContainer *ClmctfyContainerTest::GetMockContainer() {
  Container *ctnr = container_->container_;
  StrictMockContainer *mock_container = dynamic_cast<StrictMockContainer *>(ctnr);
  return mock_container;
}

TEST_F(ClmctfyContainerTest, Exec) {
  StrictMockContainer *mock_container = GetMockContainer();
  int argc = 2;
  const char *argv[] = {"echo", "hello world"};
  vector<string> cmds(argc);
  string errmsg = "some error message";
  Status status(::util::error::INTERNAL, errmsg);

  for (int i = 0; i < argc; i++) {
    cmds[i] = argv[i];
  }

  EXPECT_CALL(*mock_container, Exec(cmds))
      .WillOnce(Return(Status::OK))
      .WillOnce(Return(status));

  SHOULD_SUCCEED(lmctfy_container_exec, container_, argc, argv);
  SHOULD_FAIL_WITH_ERROR(status, lmctfy_container_exec, container_, argc, argv);
  SHOULD_BE_INVALID_ARGUMENT(lmctfy_container_exec, container_, 0, NULL);
  WITH_NULL_CONTAINER_RUN(lmctfy_container_exec, container_, argc, argv);
}

TEST_F(ClmctfyContainerTest, Update) {
  StrictMockContainer *mock_container = GetMockContainer();
  Containers__Lmctfy__ContainerSpec spec = CONTAINERS__LMCTFY__CONTAINER_SPEC__INIT;
  string errmsg = "some error message";
  Status status(::util::error::INTERNAL, errmsg);

  EXPECT_CALL(*mock_container, Update(_, Container::UPDATE_DIFF))
      .WillOnce(Return(Status::OK))
      .WillOnce(Return(status));

  int policy = CONTAINER_UPDATE_POLICY_DIFF;
  SHOULD_SUCCEED(lmctfy_container_update, container_, policy, &spec);
  SHOULD_FAIL_WITH_ERROR(status, lmctfy_container_update, container_, policy, &spec);
  SHOULD_BE_INVALID_ARGUMENT(lmctfy_container_update, container_, -1, &spec);
  WITH_NULL_CONTAINER_RUN(lmctfy_container_update, container_, -1, &spec);
}

TEST_F(ClmctfyContainerTest, Run) {
  StrictMockContainer *mock_container = GetMockContainer();
  string errmsg = "some error message";
  Status err_status(::util::error::INTERNAL, errmsg); 
  StatusOr<pid_t> statusor_success((pid_t)1);
  StatusOr<pid_t> statusor_fail(err_status);
  Containers__Lmctfy__RunSpec runspec = CONTAINERS__LMCTFY__RUN_SPEC__INIT;
  pid_t tid;
  const char *argv[] = {"/bin/echo", "hello world"};
  int argc = 2;
  vector<string> cmds(argc);
  for (int i = 0; i < argc; i++) {
    cmds[i] = argv[i];
  }

  EXPECT_CALL(*mock_container, Run(cmds, _))
      .WillOnce(Return(statusor_success))
      .WillOnce(Return(statusor_fail));

  SHOULD_SUCCEED(lmctfy_container_run, container_, argc, argv, &runspec, &tid);
  SHOULD_FAIL_WITH_ERROR(err_status, lmctfy_container_run, container_, argc, argv, &runspec, &tid);
  SHOULD_BE_INVALID_ARGUMENT(lmctfy_container_run, container_, 0, NULL, &runspec, &tid);
  WITH_NULL_CONTAINER_RUN(lmctfy_container_run, container_, argc, argv, &runspec, &tid);
}

TEST_F(ClmctfyContainerTest, Enter) {
  StrictMockContainer *mock_container = GetMockContainer();
  string errmsg = "some error message";
  Status status(::util::error::INTERNAL, errmsg); 

  EXPECT_CALL(*mock_container, Enter(_))
      .WillOnce(Return(Status::OK))
      .WillOnce(Return(status));

  pid_t tids[] = {1, 2, 3, 4};
  int n = 4;
  SHOULD_SUCCEED(lmctfy_container_enter, container_, tids, n);
  SHOULD_FAIL_WITH_ERROR(status, lmctfy_container_enter, container_, tids, n);
  // With 0 tid, it should success.
  SHOULD_SUCCEED(lmctfy_container_enter, container_, NULL, 0);
  WITH_NULL_CONTAINER_RUN(lmctfy_container_enter, container_, NULL, 0);
}

TEST_F(ClmctfyContainerTest, Spec) {
  StrictMockContainer *mock_container = GetMockContainer();
  Status status(::util::error::INTERNAL, "some error message"); 

  ContainerSpec spec;
  StatusOr<ContainerSpec> statusor_spec(spec);
  StatusOr<ContainerSpec> statusor_fail(status);

  EXPECT_CALL(*mock_container, Spec())
      .WillOnce(Return(statusor_spec))
      .WillOnce(Return(statusor_fail));

  Containers__Lmctfy__ContainerSpec *container_spec;
  SHOULD_SUCCEED(lmctfy_container_spec, container_, &container_spec);
  SHOULD_FAIL_WITH_ERROR(status, lmctfy_container_spec, container_, &container_spec);
  SHOULD_BE_INVALID_ARGUMENT(lmctfy_container_spec, container_, NULL);
  WITH_NULL_CONTAINER_RUN(lmctfy_container_spec, container_, &container_spec);
}

TEST_F(ClmctfyContainerTest, ListSubContainers) {
  StrictMockContainer *mock_container = GetMockContainer();
  Status status(::util::error::INTERNAL, "some error message"); 

  StrictMockContainer *ctnr1 = new StrictMockContainer("container1");
  StrictMockContainer *ctnr2 = new StrictMockContainer("container2");

  vector<Container *> subcontainers_vector(2);
  subcontainers_vector[0] = ctnr1;
  subcontainers_vector[1] = ctnr2;
  StatusOr<vector<Container *>> statusor_subcontainers(subcontainers_vector);
  Container::ListPolicy policy = Container::LIST_SELF;

  StatusOr<vector<Container *>> statusor_fail(status);

  EXPECT_CALL(*mock_container, ListSubcontainers(policy))
      .WillOnce(Return(statusor_subcontainers))
      .WillOnce(Return(statusor_fail));

  struct container **subcontainers;
  int nr_containers;
  SHOULD_SUCCEED(lmctfy_container_list_subcontainers, container_, CONTAINER_LIST_POLICY_SELF, &subcontainers, &nr_containers);
  EXPECT_EQ(nr_containers, subcontainers_vector.size());
  vector<Container *>::iterator iter;
  int i = 0;
  for (i = 0, iter = subcontainers_vector.begin(); iter != subcontainers_vector.end(); iter++, i++) {
    Container *ctnr = subcontainers[i]->container_;
    EXPECT_EQ(*iter, ctnr);
    lmctfy_delete_container(subcontainers[i]);
  }
  free(subcontainers);

  subcontainers = NULL;
  SHOULD_FAIL_WITH_ERROR(status, lmctfy_container_list_subcontainers, container_, CONTAINER_LIST_POLICY_SELF, &subcontainers, &nr_containers);
  EXPECT_EQ(nr_containers, 0);
  EXPECT_EQ(subcontainers, (struct container **)NULL);

  SHOULD_BE_INVALID_ARGUMENT(lmctfy_container_list_subcontainers, container_, CONTAINER_LIST_POLICY_SELF, NULL, &nr_containers);
  SHOULD_BE_INVALID_ARGUMENT(lmctfy_container_list_subcontainers, container_, CONTAINER_LIST_POLICY_SELF, &subcontainers, NULL);
  SHOULD_BE_INVALID_ARGUMENT(lmctfy_container_list_subcontainers, container_, -1, &subcontainers, &nr_containers);
  WITH_NULL_CONTAINER_RUN(lmctfy_container_list_subcontainers, container_, CONTAINER_LIST_POLICY_SELF, &subcontainers, &nr_containers);
}

TEST_F(ClmctfyContainerTest, ListThreads) {
  StrictMockContainer *mock_container = GetMockContainer();
  Status status(::util::error::INTERNAL, "some error message"); 

  int N = 10;
  vector<pid_t> pids_vector(N);
  for (int i = 0; i < N; i++) {
    pids_vector[i] = i + 1;
  }

  StatusOr<vector<pid_t>> statusor_pids(pids_vector);
  StatusOr<vector<pid_t>> statusor_fail(status);
  Container::ListPolicy policy = Container::LIST_SELF;

  EXPECT_CALL(*mock_container, ListThreads(policy))
      .WillOnce(Return(statusor_pids))
      .WillOnce(Return(statusor_fail));

  pid_t *pids;
  int nr_threads;
  SHOULD_SUCCEED(lmctfy_container_list_threads, container_, CONTAINER_LIST_POLICY_SELF, &pids, &nr_threads);
  EXPECT_EQ(nr_threads, pids_vector.size());
  vector<pid_t>::const_iterator iter;
  int i = 0;
  for (i = 0, iter = pids_vector.begin(); iter != pids_vector.end(); iter++, i++) {
    EXPECT_EQ(pids[i], *iter);
  }
  free(pids);

  pids = NULL;
  SHOULD_FAIL_WITH_ERROR(status, lmctfy_container_list_threads, container_, CONTAINER_LIST_POLICY_SELF, &pids, &nr_threads);
  EXPECT_EQ(nr_threads, 0);
  EXPECT_EQ(pids, (pid_t *)NULL);
  SHOULD_BE_INVALID_ARGUMENT(lmctfy_container_list_threads, container_, CONTAINER_LIST_POLICY_SELF, NULL, &nr_threads);
  SHOULD_BE_INVALID_ARGUMENT(lmctfy_container_list_threads, container_, CONTAINER_LIST_POLICY_SELF, &pids, NULL);
  SHOULD_BE_INVALID_ARGUMENT(lmctfy_container_list_threads, container_, -1, &pids, &nr_threads);
  WITH_NULL_CONTAINER_RUN(lmctfy_container_list_threads, container_, CONTAINER_LIST_POLICY_SELF, &pids, &nr_threads);
}

TEST_F(ClmctfyContainerTest, ListProcesses) {
  StrictMockContainer *mock_container = GetMockContainer();
  Status status(::util::error::INTERNAL, "some error message"); 

  int N = 10;
  vector<pid_t> pids_vector(N);
  for (int i = 0; i < N; i++) {
    pids_vector[i] = i + 1;
  }

  StatusOr<vector<pid_t>> statusor_pids(pids_vector);
  StatusOr<vector<pid_t>> statusor_fail(status);
  Container::ListPolicy policy = Container::LIST_SELF;

  EXPECT_CALL(*mock_container, ListProcesses(policy))
      .WillOnce(Return(statusor_pids))
      .WillOnce(Return(statusor_fail));

  pid_t *pids;
  int nr_processes;
  SHOULD_SUCCEED(lmctfy_container_list_processes, container_, CONTAINER_LIST_POLICY_SELF, &pids, &nr_processes);
  EXPECT_EQ(nr_processes, pids_vector.size());
  vector<pid_t>::const_iterator iter;
  int i = 0;
  for (i = 0, iter = pids_vector.begin(); iter != pids_vector.end(); iter++, i++) {
    EXPECT_EQ(pids[i], *iter);
  }
  free(pids);

  pids = NULL;
  SHOULD_FAIL_WITH_ERROR(status, lmctfy_container_list_processes, container_, CONTAINER_LIST_POLICY_SELF, &pids, &nr_processes);
  EXPECT_EQ(nr_processes, 0);
  EXPECT_EQ(pids, (pid_t *)NULL);
  SHOULD_BE_INVALID_ARGUMENT(lmctfy_container_list_processes, container_, CONTAINER_LIST_POLICY_SELF, NULL, &nr_processes);
  SHOULD_BE_INVALID_ARGUMENT(lmctfy_container_list_processes, container_, CONTAINER_LIST_POLICY_SELF, &pids, NULL);
  SHOULD_BE_INVALID_ARGUMENT(lmctfy_container_list_processes, container_, -1, &pids, &nr_processes);
  WITH_NULL_CONTAINER_RUN(lmctfy_container_list_processes, container_, CONTAINER_LIST_POLICY_SELF, &pids, &nr_processes);
}

TEST_F(ClmctfyContainerTest, Pause) {
  StrictMockContainer *mock_container = GetMockContainer();
  Status status(::util::error::INTERNAL, "some error message"); 

  EXPECT_CALL(*mock_container, Pause())
      .WillOnce(Return(Status::OK))
      .WillOnce(Return(status));

  SHOULD_SUCCEED(lmctfy_container_pause, container_);
  SHOULD_FAIL_WITH_ERROR(status, lmctfy_container_pause, container_);
  WITH_NULL_CONTAINER_RUN(lmctfy_container_pause, container_);
}

TEST_F(ClmctfyContainerTest, Resume) {
  StrictMockContainer *mock_container = GetMockContainer();
  Status status(::util::error::INTERNAL, "some error message"); 

  EXPECT_CALL(*mock_container, Resume())
      .WillOnce(Return(Status::OK))
      .WillOnce(Return(status));

  SHOULD_SUCCEED(lmctfy_container_resume, container_);
  SHOULD_FAIL_WITH_ERROR(status, lmctfy_container_resume, container_);
  WITH_NULL_CONTAINER_RUN(lmctfy_container_resume, container_);
}

TEST_F(ClmctfyContainerTest, KillAll) {
  StrictMockContainer *mock_container = GetMockContainer();
  Status status(::util::error::INTERNAL, "some error message"); 

  EXPECT_CALL(*mock_container, KillAll())
      .WillOnce(Return(Status::OK))
      .WillOnce(Return(status));

  SHOULD_SUCCEED(lmctfy_container_killall, container_);
  SHOULD_FAIL_WITH_ERROR(status, lmctfy_container_killall, container_);
  WITH_NULL_CONTAINER_RUN(lmctfy_container_killall, container_);
}

TEST_F(ClmctfyContainerTest, Name) {
  StrictMockContainer *mock_container = GetMockContainer();
  string container_name = mock_container->name();
  const char *name = lmctfy_container_name(container_);
  EXPECT_EQ(container_name, name);
  name = lmctfy_container_name(NULL);
  EXPECT_EQ(name, NULL);
  Container *tmp = container_->container_;
  container_->container_ = NULL;
  name = lmctfy_container_name(container_);
  EXPECT_EQ(name, NULL);
  container_->container_ = tmp;
}

static void event_callback_counter(struct container *container,
                                   const struct status *s,
                                   void *data) {
  if (data != NULL) {
    (*((int *)data))++;
  }
}

TEST_F(ClmctfyContainerTest, RegisterThenUnRegister) {
  StrictMockContainer *mock_container = GetMockContainer();
  Containers__Lmctfy__EventSpec spec = CONTAINERS__LMCTFY__EVENT_SPEC__INIT;
  notification_id_t notif_id = 0;
  StatusOr<Container::NotificationId> statusor_success((Container::NotificationId)1);
  int evt_counter = 0;

  EXPECT_CALL(*mock_container, RegisterNotification(_, _))
      .WillOnce(Return(statusor_success));

  EXPECT_CALL(*mock_container, UnregisterNotification(1))
      .WillOnce(Return(Status::OK));

  SHOULD_SUCCEED(lmctfy_container_register_notification,
                 container_,
                 event_callback_counter,
                 &evt_counter,
                 &spec,
                 &notif_id);
  EXPECT_EQ(notif_id, 1);
}

}  // namespace lmctfy
}  // namespace containers
