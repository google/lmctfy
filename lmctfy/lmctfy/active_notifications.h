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

#ifndef SRC_ACTIVE_NOTIFICATIONS_H_
#define SRC_ACTIVE_NOTIFICATIONS_H_

#include <set>
#include <string>
using ::std::string;

#include "base/integral_types.h"
#include "base/macros.h"
#include "base/mutex.h"
#include "base/thread_annotations.h"

namespace containers {
namespace lmctfy {

// Stores a set of active notification Handles.
//
// This is used to register notifications at lower levels (e.g.: Controllers)
// and have those accessible in higher layers (e.g.: ContainerApi and
// ResourceHandlers). It is also used by notification providers (e.g.:
// EventFdNotifications) to determine whether a notification is still active and
// should be delivered.
//
// Class is thread-safe.
class ActiveNotifications {
 public:
  typedef int64 Handle;

  ActiveNotifications();
  ~ActiveNotifications() {}

  // Adds a new active notification and returns its unique Handle.
  Handle Add() LOCKS_EXCLUDED(lock_);

  // Removed a notification (by Handle) from the set of active notifications.
  // Returns true if a handler was removed, false otherwise.
  bool Remove(Handle id) LOCKS_EXCLUDED(lock_);

  // Checks whether a specified notification (by Handle) is active.
  bool Contains(Handle id) const LOCKS_EXCLUDED(lock_);

  // Gets the number of active notifications.
  size_t Size() const LOCKS_EXCLUDED(lock_);

 private:
  // The next available notification Handle.
  Handle next_id_ GUARDED_BY(lock_);

  // The set of notifications that are currently registered.
  ::std::set<Handle> active_ GUARDED_BY(lock_);

  // Lock for the active notifications and their Handles.
  mutable Mutex lock_;

  DISALLOW_COPY_AND_ASSIGN(ActiveNotifications);
};

}  // namespace lmctfy
}  // namespace containers

#endif  // SRC_ACTIVE_NOTIFICATIONS_H_
