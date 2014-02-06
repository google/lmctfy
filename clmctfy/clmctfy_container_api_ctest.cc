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

#include "clmctfy_macros_ctest.h"
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
  StatusOr<Container *> statusor_container(ctnr);

  string errmsg = "some error message";
  Status status = Status(::util::error::INTERNAL, errmsg);
  StatusOr<Container *> statusor(status);

  EXPECT_CALL(*mock_api, Get(StringPiece(container_name)))
      .WillOnce(Return(statusor_container))
      .WillOnce(Return(statusor));

  SHOULD_SUCCEED(lmctfy_container_api_get_container, &container_, container_api_, container_name);
  Container *ctnr_2 = GetMockContainer();
  EXPECT_EQ(ctnr_2, ctnr);
  struct container *tmp = container_;
  container_ = NULL;
  SHOULD_FAIL_WITH_ERROR(status, lmctfy_container_api_get_container, &container_, container_api_, container_name);
  SHOULD_BE_INVALID_ARGUMENT(lmctfy_container_api_get_container, NULL, container_api_, container_name);
  SHOULD_BE_INVALID_ARGUMENT(lmctfy_container_api_get_container, &container_, container_api_, NULL);
  SHOULD_BE_INVALID_ARGUMENT(lmctfy_container_api_get_container, &container_, container_api_, "");
  WITH_NULL_CONTAINER_API_RUN(lmctfy_container_api_get_container, &container_, container_api_, container_name);
  container_ = tmp;
}

TEST_F(ClmctfyContainerApiTest, CreateContainer) {
  StrictMockContainerApi *mock_api = GetMockApi();
  const char *container_name = "test";
  Container *ctnr = new StrictMockContainer(container_name);
  StatusOr<Container *> statusor_container(ctnr);

  string errmsg = "some error message";
  Status status = Status(::util::error::INTERNAL, errmsg);
  StatusOr<Container *> statusor(status);

  EXPECT_CALL(*mock_api, Create(StringPiece(container_name), _))
      .WillOnce(Return(statusor_container))
      .WillOnce(Return(statusor));

  Containers__Lmctfy__ContainerSpec spec = CONTAINERS__LMCTFY__CONTAINER_SPEC__INIT; 

  SHOULD_SUCCEED(lmctfy_container_api_create_container, &container_, container_api_, container_name, &spec);
  Container *ctnr_2 = GetMockContainer();
  EXPECT_EQ(ctnr_2, ctnr);
  struct container *tmp = container_;
  container_ = NULL;
  SHOULD_FAIL_WITH_ERROR(status, lmctfy_container_api_create_container, &container_, container_api_, container_name, &spec);
  EXPECT_EQ(container_, (struct container *)NULL);
  SHOULD_BE_INVALID_ARGUMENT(lmctfy_container_api_create_container, NULL, container_api_, container_name, &spec);
  SHOULD_BE_INVALID_ARGUMENT(lmctfy_container_api_create_container, &container_, container_api_, container_name, NULL);
  SHOULD_BE_INVALID_ARGUMENT(lmctfy_container_api_create_container, &container_, container_api_, NULL, &spec);
  SHOULD_BE_INVALID_ARGUMENT(lmctfy_container_api_create_container, &container_, container_api_, "", &spec);
  WITH_NULL_CONTAINER_API_RUN(lmctfy_container_api_get_container, &container_, container_api_, container_name);
  container_ = tmp;
}

TEST_F(ClmctfyContainerApiTest, DestroyContainer) {
  StrictMockContainerApi *mock_api = GetMockApi();
  const char *container_name = "test";
  Container *ctnr = new StrictMockContainer(container_name);
  StatusOr<Container *> statusor_container = StatusOr<Container *>(ctnr);

  string errmsg = "some error message";
  Status destroy_status(::util::error::INTERNAL, errmsg);

  EXPECT_CALL(*mock_api, Get(StringPiece(container_name)))
      .WillOnce(Return(statusor_container))
      .WillOnce(Return(statusor_container));
  EXPECT_CALL(*mock_api, Destroy(ctnr))
      .WillOnce(Return(Status::OK))
      .WillOnce(Return(destroy_status));

  SHOULD_SUCCEED(lmctfy_container_api_get_container, &container_, container_api_, container_name);
  SHOULD_SUCCEED(lmctfy_container_api_destroy_container, container_api_, container_);

  SHOULD_SUCCEED(lmctfy_container_api_get_container, &container_, container_api_, container_name);
  SHOULD_FAIL_WITH_ERROR(destroy_status, lmctfy_container_api_destroy_container, container_api_, container_);

  SHOULD_BE_INVALID_ARGUMENT(lmctfy_container_api_destroy_container, NULL, container_);
  // XXX(monnand): Should succeed or not?
  SHOULD_SUCCEED(lmctfy_container_api_destroy_container, container_api_, NULL);
}

#define MAX_CONTAINER_NAME_LEN 512

TEST_F(ClmctfyContainerApiTest, DetectContainer) {
  StrictMockContainerApi *mock_api = GetMockApi();
  const char *container_name = "test";
  char output_name[MAX_CONTAINER_NAME_LEN];
  pid_t pid = 10;
  container_ = NULL;
  memset(output_name, 0, MAX_CONTAINER_NAME_LEN);
  StatusOr<string> statusor(container_name);

  string errmsg = "some error message";
  Status status = Status(::util::error::INTERNAL, errmsg);
  StatusOr<string> statusor_fail = StatusOr<string>(status);

  EXPECT_CALL(*mock_api, Detect(pid))
      .WillOnce(Return(statusor))
      .WillOnce(Return(statusor_fail));

  SHOULD_SUCCEED(lmctfy_container_api_detect_container, output_name, MAX_CONTAINER_NAME_LEN, container_api_, pid);
  EXPECT_EQ(string(container_name), string(output_name));

  *output_name = '\0';
  SHOULD_FAIL_WITH_ERROR(status, lmctfy_container_api_detect_container, output_name, MAX_CONTAINER_NAME_LEN, container_api_, pid);
  EXPECT_EQ(*output_name, '\0');
}
 
}  // namespace lmctfy
}  // namespace containers

