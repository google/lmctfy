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


TEST_F(ClmctfyContainerTest, ExecSuccess) {
  StrictMockContainer *mock_container = GetMockContainer();
  int argc = 2;
  const char *argv[] = {"echo", "hello world"};
  vector<string> cmds(argc);

  for (int i = 0; i < argc; i++) {
    cmds[i] = argv[i];
  }

  EXPECT_CALL(*mock_container, Exec(cmds)).WillOnce(Return(Status::OK));

  struct status s = {0, NULL};
  int ret = lmctfy_container_exec(&s, container_, argc, argv);

  EXPECT_EQ(ret, 0);
  EXPECT_EQ(s.error_code, 0);
  EXPECT_EQ(s.message, NULL);
}

}  // namespace lmctfy
}  // namespace containers
