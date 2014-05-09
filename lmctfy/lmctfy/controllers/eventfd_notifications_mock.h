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

#ifndef SRC_CONTROLLERS_EVENTFD_NOTIFICATIONS_MOCK_H_
#define SRC_CONTROLLERS_EVENTFD_NOTIFICATIONS_MOCK_H_

#include "lmctfy/controllers/eventfd_notifications.h"

#include "util/eventfd_listener_mock.h"
#include "system_api/kernel_api_mock.h"
#include "gmock/gmock.h"

namespace containers {
namespace lmctfy {

class MockEventFdNotifications : public EventFdNotifications {
 public:
  // No argument factory.
  static ::testing::StrictMock<MockEventFdNotifications> *NewStrict() {
    ::system_api::KernelAPIMock *mock_kernel =
        new ::testing::StrictMock< ::system_api::KernelAPIMock>();
    EXPECT_CALL(*mock_kernel, EpollCreate(::testing::_))
        .WillRepeatedly(::testing::Return(0));
    ::util::MockEventfdListener *mock_eventfd_listener =
        new ::testing::StrictMock< ::util::MockEventfdListener>(
            *mock_kernel, "test", nullptr, true, 0);
    EXPECT_CALL(*mock_eventfd_listener, Stop())
        .WillRepeatedly(::testing::Return());

    return new ::testing::StrictMock<MockEventFdNotifications>(
        mock_kernel, mock_eventfd_listener);
  }

  MOCK_METHOD4(RegisterNotification,
               ::util::StatusOr<ActiveNotifications::Handle>(
                   const string &cgroup_basepath, const string &cgroup_file,
                   const string &args, EventCallback *callback));

 protected:
  // It is okay to use a fake active_notifications since it is unused by the
  // mock.
  MockEventFdNotifications(const ::system_api::KernelAPIMock *mock_kernel,
                           ::util::EventfdListener *event_listener)
      : EventFdNotifications(
            reinterpret_cast<ActiveNotifications *>(0xFFFFFFFF),
            event_listener),
        mock_kernel_(mock_kernel) {}

 private:
  ::std::unique_ptr<const ::system_api::KernelAPIMock> mock_kernel_;
};

typedef ::testing::StrictMock<MockEventFdNotifications>
    StrictMockEventFdNotifications;
typedef ::testing::NiceMock<MockEventFdNotifications>
    NiceMockEventFdNotifications;

}  // namespace lmctfy
}  // namespace containers

#endif  // SRC_CONTROLLERS_EVENTFD_NOTIFICATIONS_MOCK_H_
