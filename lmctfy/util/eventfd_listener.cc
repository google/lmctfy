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

// The basic (non-multiplexed) event listener code is based off
// Documentation/cgroups/cgroup_event_listner.c from the kernel source.

#include "util/eventfd_listener.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <map>
#include <string>

#include "file/base/path.h"
#include "base/logging.h"
#include "base/walltime.h"
#include "strings/substitute.h"
#include "thread/thread_options.h"
#include "util/gtl/stl_util.h"
#include "system_api/kernel_api.h"


namespace util {

using ::file::JoinPath;
using ::std::map;
using ::std::string;
using ::std::vector;
using ::strings::Substitute;
using ::system_api::KernelAPI;

// Name of the event control file which is used for registering event
// notifications.
static const char kEventControlFile[] = "cgroup.event_control";
static const int64 kPollTimeoutMs = 200;

EventfdListener::EventfdListener(const KernelAPI &kernel,
                                 const string &thread_name,
                                 EventReceiverInterface *er,
                                 bool joinable, int max_multiplexed_events)
    : keep_running_(true),
      running_(false),
      event_receiver_(er),
      kernel_(kernel),
      max_multiplexed_events_(max_multiplexed_events) {
  SetNamePrefix(thread_name);
  SetJoinable(joinable);
  epoll_fd_ = kernel_.EpollCreate(max_multiplexed_events_);
  CHECK_GE(epoll_fd_, 0);
}

EventfdListener::~EventfdListener() {
  close(epoll_fd_);
  MutexLock lock(&mutex_);
  STLDeleteValues(&names_);
}

void EventfdListener::StopSoon() {
  MutexLock lock(&mutex_);
  keep_running_ = false;
}

void EventfdListener::Stop() {
  StopSoon();
  WaitUntilStopped();
}

void EventfdListener::WaitUntilStopped() {
  // Wait until the thread has stopped.
  mutex_.Lock();
  while (!IsNotRunningLocked()) {
    mutex_.Unlock();
    usleep(250);
    mutex_.Lock();
  }

  if (options().joinable()) {
    Join();
  }
  mutex_.Unlock();
}

void EventfdListener::Start() {
  MutexLock lock(&mutex_);
  running_ = true;
  Thread::Start();
}

bool EventfdListener::AddToEpoll(int eventfd, EventInfo *info) {
  struct epoll_event event;
  event.events = EPOLLIN;
  event.data.ptr = static_cast<void *>(info);
  if (kernel_.EpollCtl(epoll_fd_, EPOLL_CTL_ADD, eventfd, &event)) {
    LOG(ERROR) << "epoll_ctl failed for adding eventfd for "
                << " '" << info->name_ << "'";
    return false;
  }
  return true;
}

bool EventfdListener::Add(const string& basepath, const string &control_file,
                          const string &args, const string &name,
                          EventReceiverInterface *callback) {
  {
    // We're on our way out so don't accept any more events.
    MutexLock lock(&mutex_);
    if (!keep_running_)
      return false;
  }
  if (EventCount() >= max_multiplexed_events_)
    return false;
  // At least one callback (global per eventfd-listener or per event)
  // is needed. Else return false.
  if (!callback && !event_receiver_)
    return false;
  int eventfd = -1;
  // Do the setup for the eventfd w/o holding the mutex_.
  if (!SetupEvent(basepath, control_file, args, name, &eventfd))
    return false;
  MutexLock lock(&mutex_);
  EventInfo *info = new EventInfo(name, args, eventfd, callback,
      JoinPath(basepath, control_file));
  if (!AddToEpoll(eventfd, info)) {
    delete info;
    return false;
  }
  names_[eventfd] = info;
  return true;
}

int EventfdListener::EventCount() {
  MutexLock lock(&mutex_);
  return names_.size();
}

bool EventfdListener::SetupEvent(const string& basepath,
                                 const string &control_file, const string &args,
                                 const string &name, int *eventfd) {
  // Setup the eventfd notification. To register an event notification using
  // the event listener, we create an eventfd, open the control file and
  // write these args to an event_control file which sets up the
  // notifications. Once setup, we just read from the eventfd waiting for
  // events.
  string control_file_path = JoinPath(basepath, control_file);
  int control_fd = open(control_file_path.c_str(), O_RDONLY);
  if (control_fd < 0) {
    LOG(ERROR) << "Unexpected error in writing to " << control_file_path
                << "cgroup was probably destroyed for container "
                << "'" << name << "'";
    return false;
  }

  *eventfd = kernel_.Eventfd(0, EFD_CLOEXEC);
  CHECK_GE(*eventfd, 0) << "eventfd() call failed";

  string write_cmd = Substitute("$0 $1 $2", *eventfd, control_fd, args);
  string filename = JoinPath(basepath, kEventControlFile);
  bool open_error = false, write_error = false;
  int nbytes = kernel_.SafeWriteResFile(write_cmd, filename, &open_error,
                                        &write_error);
  if (nbytes < 0) {
    if (open_error && errno == ENODEV) {
      LOG(ERROR) << "cgroup destroyed for container '" << name << "'";
    } else if (write_error && errno == ENOENT) {
      LOG(ERROR) << kEventControlFile
                 << " is missing. Maybe cgroup was destroyed for container "
                 << name;
    } else {
      LOG(ERROR) << "Unexpected error in writing to " << filename
                  << " open: " << open_error << " write: " << write_error;
    }
    close(control_fd);
    kernel_.Close(*eventfd);
    return false;
  }
  CHECK_GE(nbytes, 0) << "Write to event_control file " << filename
                      << " failed. Cannot setup listener";

  close(control_fd);
  LOG(INFO) << "Starting to listen for events for control_file "
            << control_file_path << " with args " << args;
  return true;
}

void EventfdListener::ReportTermination(int eventfd, bool error) {
  string name;
  EventReceiverInterface *callback = event_receiver_;
  {
    MutexLock lock(&mutex_);
    const auto it = names_.find(eventfd);
    CHECK(it != names_.end());
    name = it->second->name_;
    if (it->second->callback_) {
      callback = it->second->callback_;
    }
    delete it->second;
    CHECK_EQ(kernel_.EpollCtl(epoll_fd_, EPOLL_CTL_DEL, eventfd, NULL), 0);
    names_.erase(it);
    kernel_.Close(eventfd);
  }
  LOG(INFO) << "Terminating eventfd listen for '" << name << "'"
            << " error = " << error;
  if (error)
    callback->ReportError(name, this);
  else
    callback->ReportExit(name, this);
}

void EventfdListener::HandlePolledEvent(struct epoll_event *events,
                                        int num_events) {
  vector<pair<int, bool>> pending_delete;

  WallTime start_time = WallTime_Now();
  for (int i = 0; i < num_events; i++) {
    if (events[i].events & EPOLLIN) {
      EventInfo *info = static_cast<EventInfo *>(events[i].data.ptr);
      WallTime elapsed_time = WallTime_Now() - start_time;
      LOG(INFO) << "Received event for " << info->name_;
      LOG_IF(INFO, elapsed_time > 1)
          << "Polled event for '" << info->name_ << "' took "
          << elapsed_time << "s to be handled";
      EventReceiverInterface *callback = event_receiver_;
      if (info->callback_) {
        callback = info->callback_;
      }

      if (kernel_.Access(info->path_, F_OK) < 0) {
        // queue up for deletion and report termination.
        pending_delete.push_back(make_pair(info->eventfd_, false));
        continue;
      }

      // Reset the eventfd counter to be able to start listening for more
      // events.
      uint64_t value;
      if (kernel_.Read(info->eventfd_, &value, sizeof(value)) < 0) {
        LOG(ERROR) << "Cannot read eventfd and reset eventfd counter";
        // queue up for deletion and report error.
        pending_delete.push_back(make_pair(info->eventfd_, true));
        continue;
      }

      if (!callback->ReportEvent(info->name_, SimpleItoa(value))) {
        LOG(ERROR) << "ReportEvent failed for '" << info->name_ << "'";
        pending_delete.push_back(make_pair(info->eventfd_, false));
        continue;
      }
    }
  }
  for (const auto& it : pending_delete)
    ReportTermination(it.first, it.second);
  pending_delete.clear();
}

void EventfdListener::TerminateAll(bool error) {
  // Copy all eventfds to a temporary vectory since we can't call
  // ReportTermination while iterating through names_ to avoid deletions from
  // invalidating the iterator.
  vector<int> eventfds;
  {
    MutexLock lock(&mutex_);
    for (auto it : names_)
      eventfds.push_back(it.first);
  }
  for (int i = 0; i < eventfds.size(); i++)
    ReportTermination(eventfds[i], error);
}

// Event notifications due to cgroup removal are received as regular
// notifications and the caller is expected to handle the case of an event being
// delivered for a cgroup that is removed and reject such an event.
void EventfdListener::Run() {
  int size;
  struct epoll_event *events = new epoll_event[max_multiplexed_events_];

  while (true) {
    {
      MutexLock lock(&mutex_);
      size = names_.size();
      if (!keep_running_)
        break;
    }
    // We exit the while() loop only on a user-action - calling Stop() or
    // StopSoon(). This avoids the race between new Add() calls and exiting out
    // of the loop when the only event gets an error.
    if (!size) {
      usleep(kPollTimeoutMs * 1000);
      continue;
    }
    int ret = kernel_.EpollWait(epoll_fd_, events, size, kPollTimeoutMs);
    if (ret == -1) {
      if (errno == EINTR)
        continue;
      LOG(ERROR) << "cannot poll from eventfds";
      // There is still a theoretical race here. If EpollWait returns an error
      // and an Add() come through before this, we may incorrectly end up
      // terminating the new event too. However, we can ignore this because
      // EpollWait failing would already mean something very wrong w/ the setup
      // and point to larger issues which should be caught very early.
      TerminateAll(true);
    }
    if (ret > 0)
      HandlePolledEvent(events, ret);
  }
  TerminateAll(false);
  delete[] events;
  {
    MutexLock lock(&mutex_);
    running_ = false;
  }
}

}  // namespace util
