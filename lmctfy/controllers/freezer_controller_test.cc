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

#include "lmctfy/controllers/freezer_controller.h"

#include <memory>

#include "system_api/kernel_api_mock.h"
#include "lmctfy/controllers/eventfd_notifications_mock.h"
#include "util/errors_test_util.h"
#include "gtest/gtest.h"

using ::system_api::KernelAPIMock;
using ::std::unique_ptr;

namespace containers {
namespace lmctfy {
namespace {

static const char kCgroupPath[] = "/dev/cgroup/freezer/test";

class FreezerControllerTest : public ::testing::Test {
 public:
  virtual void SetUp() {
    mock_kernel_.reset(new ::testing::StrictMock<KernelAPIMock>());
    mock_eventfd_notifications_.reset(MockEventFdNotifications::NewStrict());
    controller_.reset(new FreezerController(kCgroupPath, false,
                                            mock_kernel_.get(),
                                            mock_eventfd_notifications_.get()));
  }

 protected:
  unique_ptr<KernelAPIMock> mock_kernel_;
  unique_ptr<MockEventFdNotifications> mock_eventfd_notifications_;
  unique_ptr<FreezerController> controller_;
};

TEST_F(FreezerControllerTest, Type) {
  EXPECT_EQ(CGROUP_FREEZER, controller_->type());
}

TEST_F(FreezerControllerTest, Unimplemented) {
  EXPECT_ERROR_CODE(::util::error::UNIMPLEMENTED, controller_->Freeze());
  EXPECT_ERROR_CODE(::util::error::UNIMPLEMENTED, controller_->Unfreeze());
}

}  // namespace
}  // namespace lmctfy
}  // namespace containers
