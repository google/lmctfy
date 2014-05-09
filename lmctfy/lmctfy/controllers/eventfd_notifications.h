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

#ifndef SRC_CONTROLLERS_EVENTFD_NOTIFICATIONS_H_
#define SRC_CONTROLLERS_EVENTFD_NOTIFICATIONS_H_

#include <memory>
#include <string>
using ::std::string;
#include <vector>

#include "base/callback.h"
#include "base/macros.h"
#include "util/eventfd_listener.h"
#include "lmctfy/active_notifications.h"
#include "util/task/status.h"
#include "util/task/statusor.h"

namespace containers {
namespace lmctfy {

class EventReceiver;

// Registers and handles eventfd-based notifications. Specifically, those build
// around the cgroups interface.
//
// Class is thread-safe.
class EventFdNotifications {
 public:
  typedef Callback1< ::util::Status> EventCallback;

  // Takes ownersip of event_listener. Does not take ownership of
  // active_notifications.
  EventFdNotifications(ActiveNotifications *active_notifications,
                       ::util::EventfdListener *event_listener);
  virtual ~EventFdNotifications();

  // Registers an eventfd-based notification for the specified cgroup control
  // file.
  //
  // Arguments:
  //   cgroup_basepath: The base path to the cgroup_file specified (e.g.:
  //       /dev/cgroup/memory/test).
  //   cgroup_file: The cgroup control file for which to register a
  //       notifications (e.g.: memory.oom_control).
  //   args: The arguments to the event being registered (if any).
  //   callback: The callback to use for event notifications. Must not be a
  //       nullptr and must be a permanent callback.
  // Return:
  //   StatusOr: The status of the operations. Iff OK, it is populated with the
  //       Handler of the registered notification.
  virtual ::util::StatusOr<ActiveNotifications::Handle> RegisterNotification(
      const string &cgroup_basepath, const string &cgroup_file,
      const string &args, EventCallback *callback);

 private:
  // Active notifications.
  ActiveNotifications *active_notifications_;

  // Listener for eventfd-based notifications.
  ::std::unique_ptr< ::util::EventfdListener> event_listener_;

  // Created event receivers.
  ::std::vector<EventReceiver *> event_receivers_;

  DISALLOW_COPY_AND_ASSIGN(EventFdNotifications);
};

}  // namespace lmctfy
}  // namespace containers

#endif  // SRC_CONTROLLERS_EVENTFD_NOTIFICATIONS_H_
