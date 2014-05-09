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

#include "util/eventfd_listener.h"
#include "strings/substitute.h"
#include "util/gtl/stl_util.h"

using ::util::EventReceiverInterface;
using ::util::EventfdListener;
using ::std::unique_ptr;
using ::strings::Substitute;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace lmctfy {

// Custom receiver for eventfd-based notifications. Delivers notifications if
// they are still active.
//
// Class is thread-safe.
class EventReceiver : public EventReceiverInterface {
 public:
  // Does not take ownership of active_notificaitons. Takes ownership of
  // notification_callback which must be a repeatable callback.
  EventReceiver(ActiveNotifications::Handle id,
                const ActiveNotifications *active_notifications,
                Callback1<Status> *notification_callback)
      : id_(id),
        active_notifications_(CHECK_NOTNULL(active_notifications)),
        notification_callback_(CHECK_NOTNULL(notification_callback)) {
    notification_callback_->IsRepeatable();
  }
  ~EventReceiver() {}

  // Deliver the event to the user. Stop reporting the event if the notification
  // was unregistered.
  bool ReportEvent(const string &name, const string &args) {
    // If not active anymore, return false (unregisters notification).
    if (!active_notifications_->Contains(id_)) {
      return false;
    }

    // Deliver event to the user.
    notification_callback_->Run(Status::OK);
    return true;
  }

  // Report the error to the user.
  void ReportError(const string &name, EventfdListener *listener) {
    // Error, report it. This is de-registered.
    LOG(WARNING) << "No longer notifying for event with Handle: " << id_
                 << " due to error";

    // Notify the user of the error.
    notification_callback_->Run(
        Status(::util::error::CANCELLED,
               Substitute("Failed to register event with Handle \"$0\"", id_)));
  }

  // Log the exit.
  void ReportExit(const string &name, EventfdListener *listener) {
    // We shut ourselves down.
    LOG(INFO) << "No longer notifying for event with Handle: " << id_;
  }

 private:
  // The Handle of the notification this listener listens for.
  const ActiveNotifications::Handle id_;

  // Notifications active in the system.
  const ActiveNotifications *active_notifications_;

  // The callback used to deliver notificaitons to the user.
  unique_ptr<Callback1<Status>> notification_callback_;

  DISALLOW_COPY_AND_ASSIGN(EventReceiver);
};

EventFdNotifications::EventFdNotifications(
    ActiveNotifications *active_notifications,
    ::util::EventfdListener *event_listener)
    : active_notifications_(CHECK_NOTNULL(active_notifications)),
      event_listener_(CHECK_NOTNULL(event_listener)) {}

EventFdNotifications::~EventFdNotifications() {
  event_listener_->Stop();
  event_listener_.reset();

  // TODO(vmarmol): Use something that doesn't require us to keep track of the
  // receivers since they're staying around for longer than they need to be.
  STLDeleteElements(&event_receivers_);
}

StatusOr<ActiveNotifications::Handle>
EventFdNotifications::RegisterNotification(const string &cgroup_basepath,
                                           const string &cgroup_file,
                                           const string &args,
                                           EventCallback *callback) {
  CHECK_NOTNULL(callback);
  callback->CheckIsRepeatable();

  // Get a Handle for this event.
  ActiveNotifications::Handle id = active_notifications_->Add();

  // Register the event with the eventfd-based listener.
  unique_ptr<EventReceiver> receiver(
      new EventReceiver(id, active_notifications_, callback));
  if (!event_listener_->Add(cgroup_basepath, cgroup_file, args, "",
                            receiver.get())) {
    return Status(::util::error::INTERNAL,
                  "Failed to register listener for the event");
  }
  event_receivers_.push_back(receiver.release());

  // Start listener thread if it was not already running.
  if (event_listener_->IsNotRunning()) {
    event_listener_->Start();
  }

  return id;
}

}  // namespace lmctfy
}  // namespace containers
