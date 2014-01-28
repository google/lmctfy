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

#include "clmctfy.h"

#include "lmctfy.h"
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

namespace containers {
namespace lmctfy {

StatusOr<ContainerApi *> ContainerApi::New() {
  return new StrictMockContainerApi();
}

Status ContainerApi::InitMachine(const InitSpec &spec) {
  return Status::OK;
}

class ClmctfyContainerApiTest : public ::testing::Test {
 public:
  virtual void SetUp() {
    container_api_ = NULL;
    container_ = NULL;
    lmctfy_new_container_api(NULL, &container_api_);
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

StrictMockContainerApi *ClmctfyContainerApiTest::GetMockApi() {
  ContainerApi *capi = internal::lmctfy_container_api_strip(container_api_);
  StrictMockContainerApi *mock_api = dynamic_cast<StrictMockContainerApi *>(capi);
  return mock_api;
}

StrictMockContainer *ClmctfyContainerApiTest::GetMockContainer() {
  Container *ctnr = internal::lmctfy_container_strip(container_);
  StrictMockContainer *mock_container = dynamic_cast<StrictMockContainer *>(ctnr);
  return mock_container;
}

TEST_F(ClmctfyContainerApiTest, GetContainer) {
  StrictMockContainerApi *mock_api = GetMockApi();
  const char *container_name = "test";
  Container *ctnr = new StrictMockContainer(container_name);
  StatusOr<Container *> statusor_container = StatusOr<Container *>(ctnr);

  EXPECT_CALL(*mock_api, Get(StringPiece(container_name))).WillOnce(Return(statusor_container));

  struct status s = {0, NULL};
  int ret = lmctfy_container_api_get_container(&s, &container_, container_api_, container_name);

  EXPECT_EQ(ret, 0);
  EXPECT_EQ(s.error_code, 0);
  EXPECT_EQ(s.message, NULL);
  Container *ctnr_2 = GetMockContainer();
  EXPECT_EQ(ctnr_2, ctnr);
}

TEST_F(ClmctfyContainerApiTest, GetContainerFail) {
  StrictMockContainerApi *mock_api = GetMockApi();
  const char *container_name = "test";
  string errmsg = "some error message";
  Status status = Status(::util::error::INTERNAL, errmsg);
  StatusOr<Container *> statusor = StatusOr<Container *>(status);

  EXPECT_CALL(*mock_api, Get(StringPiece(container_name))).WillOnce(Return(statusor));

  container_ = NULL;
  struct status s = {0, NULL};
  int ret = lmctfy_container_api_get_container(&s, &container_, container_api_, container_name);

  EXPECT_EQ(ret, ::util::error::INTERNAL);
  EXPECT_EQ(s.error_code, ::util::error::INTERNAL);
  EXPECT_EQ(container_, NULL);
  EXPECT_EQ(errmsg, s.message);
  free(s.message);
}

TEST_F(ClmctfyContainerApiTest, CreateContainer) {
  StrictMockContainerApi *mock_api = GetMockApi();
  const char *container_name = "test";
  Container *ctnr = new StrictMockContainer(container_name);
  StatusOr<Container *> statusor_container = StatusOr<Container *>(ctnr);

  EXPECT_CALL(*mock_api, Create(StringPiece(container_name), _)).WillOnce(Return(statusor_container));

  Containers__Lmctfy__ContainerSpec spec = CONTAINERS__LMCTFY__CONTAINER_SPEC__INIT; 

  struct status s = {0, NULL};
  int ret = lmctfy_container_api_create_container(&s, &container_, container_api_, container_name, &spec);

  EXPECT_EQ(ret, 0);
  EXPECT_EQ(s.error_code, 0);
  EXPECT_EQ(s.message, NULL);
  Container *ctnr_2 = GetMockContainer();
  EXPECT_EQ(ctnr_2, ctnr);
}

TEST_F(ClmctfyContainerApiTest, CreateContainerFail) {
  StrictMockContainerApi *mock_api = GetMockApi();
  const char *container_name = "test";
  string errmsg = "some error message";
  Status status = Status(::util::error::INTERNAL, errmsg);
  StatusOr<Container *> statusor = StatusOr<Container *>(status);

  EXPECT_CALL(*mock_api, Create(StringPiece(container_name), _)).WillOnce(Return(statusor));

  Containers__Lmctfy__ContainerSpec spec = CONTAINERS__LMCTFY__CONTAINER_SPEC__INIT; 
  struct status s = {0, NULL};
  int ret = lmctfy_container_api_create_container(&s, &container_, container_api_, container_name, &spec);

  EXPECT_EQ(ret, ::util::error::INTERNAL);
  EXPECT_EQ(s.error_code, ::util::error::INTERNAL);
  EXPECT_EQ(container_, NULL);
  EXPECT_EQ(errmsg, s.message);
  free(s.message);
}

TEST_F(ClmctfyContainerApiTest, DestroyContainer) {
  StrictMockContainerApi *mock_api = GetMockApi();
  const char *container_name = "test";
  Container *ctnr = new StrictMockContainer(container_name);
  StatusOr<Container *> statusor_container = StatusOr<Container *>(ctnr);

  EXPECT_CALL(*mock_api, Get(StringPiece(container_name))).WillOnce(Return(statusor_container));
  EXPECT_CALL(*mock_api, Destroy(ctnr)).WillOnce(Return(Status::OK));

  struct status s = {0, NULL};
  int ret = lmctfy_container_api_get_container(&s, &container_, container_api_, container_name);

  EXPECT_EQ(ret, 0);
  EXPECT_EQ(s.error_code, 0);
  EXPECT_EQ(s.message, NULL);

  ret = lmctfy_container_api_destroy_container(&s, container_api_, container_);
  container_ = NULL;
  EXPECT_EQ(ret, 0);
  EXPECT_EQ(s.error_code, 0);
  EXPECT_EQ(s.message, NULL);
}

TEST_F(ClmctfyContainerApiTest, DestroyContainerFail) {
  StrictMockContainerApi *mock_api = GetMockApi();
  const char *container_name = "test";
  Container *ctnr = new StrictMockContainer(container_name);
  StatusOr<Container *> statusor_container = StatusOr<Container *>(ctnr);

  string errmsg = "some error message";
  Status destroy_status = Status(::util::error::INTERNAL, errmsg);

  EXPECT_CALL(*mock_api, Get(StringPiece(container_name))).WillOnce(Return(statusor_container));
  EXPECT_CALL(*mock_api, Destroy(ctnr)).WillOnce(Return(destroy_status));

  struct status s = {0, NULL};
  int ret = lmctfy_container_api_get_container(&s, &container_, container_api_, container_name);

  EXPECT_EQ(ret, 0);
  EXPECT_EQ(s.error_code, 0);
  EXPECT_EQ(s.message, NULL);

  ret = lmctfy_container_api_destroy_container(&s, container_api_, container_);
  container_ = NULL;
  EXPECT_EQ(ret, ::util::error::INTERNAL);
  EXPECT_EQ(s.error_code, ::util::error::INTERNAL);
  EXPECT_EQ(errmsg, s.message);
}

#define MAX_CONTAINER_NAME_LEN 512

TEST_F(ClmctfyContainerApiTest, DetectContainer) {
  StrictMockContainerApi *mock_api = GetMockApi();
  const char *container_name = "test";
  char output_name[MAX_CONTAINER_NAME_LEN];
  pid_t pid = 10;
  container_ = NULL;

  memset(output_name, 0, MAX_CONTAINER_NAME_LEN);

  StatusOr<string> statusor = StatusOr<string>(container_name);

  EXPECT_CALL(*mock_api, Detect(pid)).WillOnce(Return(statusor));

  struct status s = {0, NULL};
  int ret = lmctfy_container_api_detect_container(&s, output_name, MAX_CONTAINER_NAME_LEN, container_api_, pid);

  EXPECT_EQ(ret, 0);
  EXPECT_EQ(s.error_code, 0);
  EXPECT_EQ(s.message, NULL);
  EXPECT_EQ(string(container_name), string(output_name));
}
 
TEST_F(ClmctfyContainerApiTest, DetectContainerFail) {
  StrictMockContainerApi *mock_api = GetMockApi();
  const char *container_name = "test";
  char output_name[MAX_CONTAINER_NAME_LEN];
  pid_t pid = 10;
  container_ = NULL;

  memset(output_name, 0, MAX_CONTAINER_NAME_LEN);

  string errmsg = "some error message";
  Status status = Status(::util::error::INTERNAL, errmsg);
  StatusOr<string> statusor = StatusOr<string>(status);

  EXPECT_CALL(*mock_api, Detect(pid)).WillOnce(Return(statusor));

  struct status s = {0, NULL};
  int ret = lmctfy_container_api_detect_container(&s, output_name, MAX_CONTAINER_NAME_LEN, container_api_, pid);

  EXPECT_EQ(ret, status.error_code());
  EXPECT_EQ(s.error_code, status.error_code());
  EXPECT_EQ(errmsg, s.message);
  EXPECT_EQ(*output_name, '\0');
}
 
}  // namespace lmctfy
}  // namespace containers

