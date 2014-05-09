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

#include "lmctfy/controllers/perf_controller.h"

#include <memory>

#include "system_api/kernel_api_mock.h"
#include "lmctfy/controllers/eventfd_notifications_mock.h"
#include "lmctfy/kernel_files.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::testing::StrictMock;

namespace containers {
namespace lmctfy {

static const char kMountPoint[] = "/dev/cgroup/perf_event/test";
static const char kHierarchyPath[] = "/test";

class PerfControllerTest : public ::testing::Test {
 public:
  virtual void SetUp() {
    mock_kernel_.reset(new StrictMock< ::system_api::KernelAPIMock>());
    mock_eventfd_notifications_.reset(MockEventFdNotifications::NewStrict());
    controller_.reset(new PerfController(kHierarchyPath, kMountPoint, true,
                                         mock_kernel_.get(),
                                         mock_eventfd_notifications_.get()));
  }

 protected:
  ::std::unique_ptr< ::system_api::KernelAPIMock> mock_kernel_;
  ::std::unique_ptr<PerfController> controller_;
  ::std::unique_ptr<MockEventFdNotifications> mock_eventfd_notifications_;
};

TEST_F(PerfControllerTest, Type) {
  EXPECT_EQ(CGROUP_PERF_EVENT, controller_->type());
}

TEST_F(PerfControllerTest, CreatesController) {
  EXPECT_TRUE(controller_.get() != NULL);
}

}  // namespace lmctfy
}  // namespace containers
