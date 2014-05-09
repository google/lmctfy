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

#include "lmctfy/controllers/eventfd_notifications.h"

#include <memory>
#include <string>
using ::std::string;

#include "util/eventfd_listener_mock.h"
#include "system_api/kernel_api_mock.h"
#include "lmctfy/active_notifications.h"
#include "util/errors_test_util.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::util::EventfdListener;
using ::util::MockEventfdListener;
using ::system_api::KernelAPIMock;
using ::std::unique_ptr;
using ::testing::NotNull;
using ::testing::Return;
using ::testing::StrictMock;
using ::testing::_;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace lmctfy {
namespace {

static const char kCgroupPath[] = "/dev/cgroup/memory/test";
static const char kCgroupFile[] = "memory.oom_control";
static const char kArg[] = "1024";

class EventfdNotificationsTest : public ::testing::Test {
 public:
  virtual void SetUp() {
    mock_kernel_.reset(new StrictMock<KernelAPIMock>());
    active_notifications_.reset(new ActiveNotifications());
    EXPECT_CALL(*mock_kernel_, EpollCreate(_))
        .WillRepeatedly(Return(0));
    mock_eventfd_listener_ = new StrictMock<MockEventfdListener>(
        *mock_kernel_, "test", nullptr, true, 0);
    EXPECT_CALL(*mock_eventfd_listener_, Stop())
        .WillRepeatedly(Return());
    EXPECT_CALL(*mock_eventfd_listener_, IsNotRunning())
        .WillRepeatedly(Return(true));

    notifications_.reset(new EventFdNotifications(active_notifications_.get(),
                                                  mock_eventfd_listener_));
  }

 protected:
  unique_ptr<ActiveNotifications> active_notifications_;
  unique_ptr<KernelAPIMock> mock_kernel_;
  MockEventfdListener *mock_eventfd_listener_;
  unique_ptr<EventFdNotifications> notifications_;
};

// Dummy callback that should never be called.
void EventCallback(Status status) {
  CHECK(false) << "Should never be called";
}

TEST_F(EventfdNotificationsTest, RegisterNotificationSuccess) {
  EXPECT_CALL(*mock_eventfd_listener_, Add(kCgroupPath, kCgroupFile, kArg, "",
                                           NotNull())).WillOnce(Return(true));
  EXPECT_CALL(*mock_eventfd_listener_, Start())
      .WillOnce(Return());

  StatusOr<ActiveNotifications::Handle> statusor =
      notifications_->RegisterNotification(
          kCgroupPath, kCgroupFile, kArg, NewPermanentCallback(&EventCallback));
  ASSERT_OK(statusor);
  EXPECT_LT(0, statusor.ValueOrDie());
}

TEST_F(EventfdNotificationsTest, RegisterNotificationFails) {
  EXPECT_CALL(*mock_eventfd_listener_, Add(kCgroupPath, kCgroupFile, kArg, "",
                                           NotNull())).WillOnce(Return(false));

  EXPECT_ERROR_CODE(::util::error::INTERNAL,
                    notifications_->RegisterNotification(
                        kCgroupPath, kCgroupFile, kArg,
                        NewPermanentCallback(&EventCallback)));
}

TEST_F(EventfdNotificationsTest, RegisterNotificationBadCallback) {
  EXPECT_DEATH(notifications_->RegisterNotification("", "", "", nullptr),
               "Must be non NULL");
  EXPECT_DEATH(notifications_->RegisterNotification(
                   "", "", "", NewCallback(&EventCallback)),
               "not a repeatable callback");
}

}  // namespace
}  // namespace lmctfy
}  // namespace containers
