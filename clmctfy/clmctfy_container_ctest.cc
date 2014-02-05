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

#include <vector>

#include "clmctfy.h"

#include "lmctfy_mock.h"
#include "gtest/gtest.h"
#include "gmock/gmock.h"
#include "util/task/statusor.h"
#include "clmctfy_internal.h"
#include "strings/stringpiece.h"

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
    const char *container_name = "test";
    lmctfy_new_container_api(NULL, &container_api_);
    StrictMockContainerApi *mock_api = GetMockApi();
    Container *ctnr = new StrictMockContainer(container_name);
    StatusOr<Container *> statusor = StatusOr<Container *>(ctnr);
    EXPECT_CALL(*mock_api, Get(StringPiece(container_name))).WillOnce(Return(statusor));

    lmctfy_container_api_get_container(NULL,
                                       &container_,
                                       container_api_,
                                       container_name);
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
};

StrictMockContainerApi *ClmctfyContainerTest::GetMockApi() {
  ContainerApi *capi = internal::lmctfy_container_api_strip(container_api_);
  StrictMockContainerApi *mock_api = dynamic_cast<StrictMockContainerApi *>(capi);
  return mock_api;
}

StrictMockContainer *ClmctfyContainerTest::GetMockContainer() {
  Container *ctnr = internal::lmctfy_container_strip(container_);
  StrictMockContainer *mock_container = dynamic_cast<StrictMockContainer *>(ctnr);
  return mock_container;
}

TEST_F(ClmctfyContainerTest, Exec) {
  StrictMockContainer *mock_container = GetMockContainer();
  int argc = 2;
  const char *argv[] = {"echo", "hello world"};
  vector<string> cmds(argc);
  string errmsg = "some error message";
  Status status = Status(::util::error::INTERNAL, errmsg);

  for (int i = 0; i < argc; i++) {
    cmds[i] = argv[i];
  }

  EXPECT_CALL(*mock_container, Exec(cmds))
      .WillOnce(Return(Status::OK))
      .WillOnce(Return(status));

  // First call will be success.
  struct status s = {0, NULL};
  int ret = lmctfy_container_exec(&s, container_, argc, argv);

  EXPECT_EQ(ret, 0);
  EXPECT_EQ(s.error_code, 0);
  EXPECT_EQ(s.message, NULL);

  // second call failed.
  s = {0, NULL};
  ret = lmctfy_container_exec(&s, container_, argc, argv);

  EXPECT_EQ(ret, s.error_code);
  EXPECT_EQ(s.error_code, status.error_code());
  EXPECT_EQ(errmsg, s.message);

  // Invalid arguments.
  s = {0, NULL};
  ret = lmctfy_container_exec(&s, container_, 0, NULL);

  EXPECT_EQ(ret, s.error_code);
  EXPECT_EQ(s.error_code, UTIL__ERROR__CODE__INVALID_ARGUMENT);

  Container *tmp = container_->container_;
  container_->container_ = NULL;
  s = {0, NULL};
  ret = lmctfy_container_exec(&s, container_, 0, NULL);
  container_->container_ = tmp;
  EXPECT_EQ(ret, s.error_code);
  EXPECT_EQ(s.error_code, UTIL__ERROR__CODE__INVALID_ARGUMENT);

  s = {0, NULL};
  ret = lmctfy_container_exec(&s, NULL, 0, NULL);
  container_->container_ = tmp;
  EXPECT_EQ(ret, s.error_code);
  EXPECT_EQ(s.error_code, UTIL__ERROR__CODE__INVALID_ARGUMENT);

}

TEST_F(ClmctfyContainerTest, Update) {
  StrictMockContainer *mock_container = GetMockContainer();
  Containers__Lmctfy__ContainerSpec spec = CONTAINERS__LMCTFY__CONTAINER_SPEC__INIT;
  string errmsg = "some error message";
  Status status = Status(::util::error::INTERNAL, errmsg);

  EXPECT_CALL(*mock_container, Update(_, Container::UPDATE_DIFF))
      .WillOnce(Return(Status::OK))
      .WillOnce(Return(status));

  struct status s = {0, NULL};
  int policy = CONTAINER_UPDATE_POLICY_DIFF;
  int ret = lmctfy_container_update(&s, container_, policy, &spec);

  EXPECT_EQ(ret, 0);
  EXPECT_EQ(s.error_code, 0);
  EXPECT_EQ(s.message, NULL);

  s = {0, NULL};
  ret = lmctfy_container_update(&s, container_, policy, &spec);
  EXPECT_EQ(ret, s.error_code);
  EXPECT_EQ(s.error_code, status.error_code());
  EXPECT_EQ(errmsg, s.message);

  s = {0, NULL};
  ret = lmctfy_container_update(&s, container_, -1, &spec);
  EXPECT_EQ(ret, s.error_code);
  EXPECT_EQ(s.error_code, UTIL__ERROR__CODE__INVALID_ARGUMENT);

  Container *tmp = container_->container_;
  container_->container_ = NULL;
  s = {0, NULL};
  ret = lmctfy_container_update(&s, container_, policy, &spec);
  container_->container_ = tmp;
  EXPECT_EQ(ret, s.error_code);
  EXPECT_EQ(s.error_code, UTIL__ERROR__CODE__INVALID_ARGUMENT);

  s = {0, NULL};
  ret = lmctfy_container_update(&s, NULL, policy, &spec);
  container_->container_ = tmp;
  EXPECT_EQ(ret, s.error_code);
  EXPECT_EQ(s.error_code, UTIL__ERROR__CODE__INVALID_ARGUMENT);
}

TEST_F(ClmctfyContainerTest, Enter) {
  StrictMockContainer *mock_container = GetMockContainer();
  Containers__Lmctfy__ContainerSpec spec = CONTAINERS__LMCTFY__CONTAINER_SPEC__INIT;
  string errmsg = "some error message";
  Status status = Status(::util::error::INTERNAL, errmsg); 

  EXPECT_CALL(*mock_container, Enter(_))
      .WillOnce(Return(Status::OK))
      .WillOnce(Return(status));

  struct status s = {0, NULL};
  pid_t tids[] = {1, 2, 3, 4};
  int n = 4;
  int ret = lmctfy_container_enter(&s, container_, tids, n);
  EXPECT_EQ(ret, 0);
  EXPECT_EQ(s.error_code, 0);
  EXPECT_EQ(s.message, NULL);

  s = {0, NULL};
  ret = lmctfy_container_enter(&s, container_, tids, n);
  EXPECT_EQ(ret, s.error_code);
  EXPECT_EQ(s.error_code, status.error_code());
  EXPECT_EQ(errmsg, s.message);

  s = {0, NULL};
  ret = lmctfy_container_enter(&s, container_, tids, -1);
  EXPECT_EQ(ret, s.error_code);
  EXPECT_EQ(s.error_code, UTIL__ERROR__CODE__OK);

  s = {0, NULL};
  ret = lmctfy_container_enter(&s, container_, NULL, 0);
  EXPECT_EQ(ret, s.error_code);
  EXPECT_EQ(s.error_code, UTIL__ERROR__CODE__OK);

  s = {0, NULL};
  ret = lmctfy_container_enter(&s, container_, tids, 0);
  EXPECT_EQ(ret, s.error_code);
  EXPECT_EQ(s.error_code, UTIL__ERROR__CODE__OK);

  Container *tmp = container_->container_;
  container_->container_ = NULL;
  s = {0, NULL};
  ret = lmctfy_container_enter(&s, container_, tids, n);
  container_->container_ = tmp;
  EXPECT_EQ(ret, s.error_code);
  EXPECT_EQ(s.error_code, UTIL__ERROR__CODE__INVALID_ARGUMENT);

  s = {0, NULL};
  ret = lmctfy_container_enter(&s, NULL, tids, n);
  container_->container_ = tmp;
  EXPECT_EQ(ret, s.error_code);
  EXPECT_EQ(s.error_code, UTIL__ERROR__CODE__INVALID_ARGUMENT);
}

}  // namespace lmctfy
}  // namespace containers
