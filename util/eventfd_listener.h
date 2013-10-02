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

// Helper class that registers an eventfd against a control file and listens for
// any notifications. This is used with the cgroup event_control notification
// API.
// Add(): Register an event to be listened for by this thread. Sets up the
//        eventfd and such for the event. Returns false if setup fails or if
//        this exceeds the max_multiplexed_events value.
// Start(): Start the event listen loop for added eventfds.
// EventCount(): Number of registered events.
// StopSoon(): Notifies the thread that it should stop soon.
// Stop(): Waits for notification thread to exit.
// Interesting event: Invokes ReportEvent and goes back to listening for
//         more events.
// Errors while listening: Invokes ReportError
// Exit: Invokes ReportExit
// If Start() has been called, note that it is safe to destroy the object only
// after ReportError or ReportExit are invoked.

#ifndef UTIL_EVENTFD_LISTENER_H_
#define UTIL_EVENTFD_LISTENER_H_

#include <sys/epoll.h>

#include <hash_map>
#include <map>
#include <string>
#include <vector>

#include "base/mutex.h"
#include "base/thread_annotations.h"
#include "thread/thread.h"

namespace system_api {
class KernelAPI;
}  // namespace system_api

namespace util {

using ::std::map;
using ::std::string;
using ::std::vector;

class EventfdListener;

// Callback interface for receiving events from any eventfd control files
// registered with an EventfdListener.
class EventReceiverInterface {
 public:
  // Invoked in response to any event. Return true to continue monitoring, or
  // false to exit.
  virtual bool ReportEvent(const string &name, const string &args) = 0;

  // Invoked when the eventfd is deleted in response to an internal error
  // (e.g. when the eventfd counter cannot be reset).
  virtual void ReportError(const string &name, EventfdListener *efdl) = 0;

  // Invoked when the eventfd is deleted in response to the shutdown signal from
  // ReportEvent() (e.g. when it returns false).
  virtual void ReportExit(const string &name, EventfdListener *efdl) = 0;

  virtual ~EventReceiverInterface() {}
};

struct EventInfo {
  EventInfo(const string& name, const string& args, int eventfd,
      EventReceiverInterface *callback, const string& control_file_path)
    : name_(name),
      args_(args),
      eventfd_(eventfd),
      callback_(callback),
      path_(control_file_path) {}

  const string name_;
  const string args_;
  int eventfd_;
  EventReceiverInterface *callback_;
  const string path_;
};

// This helps get notifications when the interesting event happens. This is
// based on the kernel eventfd() mechansism.
class EventfdListener : public Thread {
 public:
  // kernel: Interface for kernel operations that may be mocked out for testing
  // thread_name: identifier for this thread.
  // er: Event reporting object
  // joinable: Whether this thread should be marked joinable or not.
  // max_multiplexed_events: Maximum number of events that can be listened for
  //                         at the same time by this thread. Will reject any
  //                         further Add() calls after that number is reached.
  EventfdListener(const ::system_api::KernelAPI& kernel,
                  const string& thread_name,
                  EventReceiverInterface* er,
                  bool joinable,
                  int max_multiplexed_events);
  // Note that it is safe to destroy the object only after ReportError or
  // ReportExit are invoked, if Start() has been called.
  // Will die if the notification thread is still running.
  virtual ~EventfdListener();

  // basepath: base path of the container
  // control_file: control_file path for the interesting event. This is prefixed
  //               with basepath
  // args: arguments to pass to the event_control file
  // name: identifier to pass back to the Report* calls
  // Add a new event to be listented to. Returns false on either setup errors
  // for the new event of if max_multiplexed_events_ has been reached already.
  // Returns false if no callback is provided and there is no global callback
  // registered when creating this object.
  virtual bool Add(
    const string &basepath, const string &control_file,
    const string &args, const string &name,
    EventReceiverInterface *callback) LOCKS_EXCLUDED(mutex_);
  // Return the number of events being listened to.
  // When reporting error or exit events, this will already be decremented for
  // the event in question.
  virtual int EventCount() LOCKS_EXCLUDED(mutex_);
  // The Start() and Stop() calls need to be perfectly interleaved.
  virtual void Start() LOCKS_EXCLUDED(mutex_);
  // Non-blocking Stop. Will signal the thread to stop by clearing keep_running_
  // and return.
  virtual void StopSoon() LOCKS_EXCLUDED(mutex_);
  // Blocking Stop. Caller cannot call this successively without calls to
  // Start() in between.
  virtual void Stop();
  // Waits for the notification thread to stop.
  void WaitUntilStopped() LOCKS_EXCLUDED(mutex_);
  // Returns true when the notification thread is not running.
  bool IsNotRunning() LOCKS_EXCLUDED(mutex_) {
    MutexLock lock(&mutex_);
    return IsNotRunningLocked();
  }

 protected:
  virtual void Run() LOCKS_EXCLUDED(mutex_);
  // Setup the notification using eventfd. This will be invoked at Start() and
  // each time Update() is invoked.
  bool SetupEvent(const string &basepath, const string &control_file,
                  const string &args, const string &name, int *eventfd);
  // Remove the event from the internal data structures and report termination
  // for that event (error or exit).
  void ReportTermination(int index, bool error) LOCKS_EXCLUDED(mutex_);

  bool keep_running_ GUARDED_BY(mutex_);  // notifies the Run() loop to stop
  bool running_ GUARDED_BY(mutex_);  // true if the notification thread is
                                     // running
  EventReceiverInterface *event_receiver_;

 private:
  bool AddToEpoll(int eventfd, EventInfo *info);
  void HandlePolledEvent(struct epoll_event *events, int size);
  void TerminateAll(bool error);
  // Returns true when the notification thread is not running.
  bool IsNotRunningLocked() const EXCLUSIVE_LOCKS_REQUIRED(mutex_) {
    return !running_;
  }

  const ::system_api::KernelAPI &kernel_;
  int max_multiplexed_events_;  // maximum number of events that can be added.
  // Map of eventfd to EventInfo. We have eventfd as the key and also present in
  // EventInfo since it allows us to process normal events (not errors) w/o
  // needing to grab the Mutex by passing in EventInfo to epoll_ctl().
  map<int, EventInfo *> names_ GUARDED_BY(mutex_);
  int epoll_fd_;
  Mutex mutex_;
};

class EventfdListenerFactory {
 public:
  // Caller takes over ownership of EventfdListener object.
  virtual EventfdListener *NewEventfdListener(
      const ::system_api::KernelAPI &kernel, const string &thread_name,
      EventReceiverInterface *er, bool joinable, int max_multiplexed_events) {
    return new EventfdListener(kernel, thread_name, er, joinable,
                               max_multiplexed_events);
  }

  // Enable mocking of deletion of eventfd listener. Needed by SetupNewCpuset
  // of MemclmctfyerTest.
  virtual void DeleteEventfdListener(EventfdListener *eventfd_listener) {
    if (eventfd_listener) {
      delete eventfd_listener;
    }
  }

  virtual ~EventfdListenerFactory() {}
};

}  // namespace util

#endif  // UTIL_EVENTFD_LISTENER_H_
