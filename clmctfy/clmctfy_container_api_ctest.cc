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

#include "lmctfy_mock.h"
#include "gtest/gtest.h"
#include "gmock/gmock.h"
#include "util/task/statusor.h"
#include "clmctfy_internal.h"

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
    lmctfy_container_api_destroy_container(NULL, container_api_, container_);
    lmctfy_delete_container_api(container_api_);
  }
 protected:
  struct container_api *container_api_;
  struct container *container_;
  StrictMockContainerApi *GetMockApi();
};

StrictMockContainerApi *ClmctfyContainerApiTest::GetMockApi() {
  ContainerApi *capi = internal::lmctfy_container_api_strip(container_api_);
  StrictMockContainerApi *mock_api = dynamic_cast<StrictMockContainerApi *>(capi);
  return mock_api;
}

TEST_F(ClmctfyContainerApiTest, GetContainer) {
  StrictMockContainerApi *mock_api = GetMockApi();
  const char *container_name = "test";
  Container *ctnr = new StrictMockContainer(container_name);
  StatusOr<Container *> statusor_container = StatusOr<Container *>(ctnr);
  EXPECT_CALL(*mock_api, Get(_)).WillOnce(Return(statusor_container));
  struct container *c = NULL;
  int ret = lmctfy_container_api_get_container(NULL, &c, container_api_, container_name);
  EXPECT_EQ(ret, 0);
  Container *ctnr_2 = internal::lmctfy_container_strip(c);
  EXPECT_EQ(ctnr_2, ctnr);
}

TEST_F(ClmctfyContainerApiTest, CreateContainer) {
  StrictMockContainerApi *mock_api = GetMockApi();
  const char *container_name = "test";
  Container *ctnr = new StrictMockContainer(container_name);
  StatusOr<Container *> statusor_container = StatusOr<Container *>(ctnr);
  EXPECT_CALL(*mock_api, Create(_, _)).WillOnce(Return(statusor_container));
  struct container *c = NULL;
  Containers__Lmctfy__ContainerSpec spec = CONTAINERS__LMCTFY__CONTAINER_SPEC__INIT; 
  int ret = lmctfy_container_api_create_container(NULL, &c, container_api_, container_name, &spec);
  EXPECT_EQ(ret, 0);
  Container *ctnr_2 = internal::lmctfy_container_strip(c);
  EXPECT_EQ(ctnr_2, ctnr);
}

}  // namespace lmctfy
}  // namespace containers

