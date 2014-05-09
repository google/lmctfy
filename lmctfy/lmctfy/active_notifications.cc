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

#include "lmctfy/active_notifications.h"

#include "base/mutex.h"

namespace containers {
namespace lmctfy {

ActiveNotifications::ActiveNotifications() : next_id_(1) {}

ActiveNotifications::Handle ActiveNotifications::Add() {
  MutexLock l(&lock_);

  Handle id = next_id_++;
  active_.insert(id);
  return id;
}

bool ActiveNotifications::Remove(ActiveNotifications::Handle id) {
  MutexLock l(&lock_);

  // If an Handle was removed, it existed.
  return active_.erase(id) != 0;
}

bool ActiveNotifications::Contains(ActiveNotifications::Handle id) const {
  MutexLock l(&lock_);

  for (const auto &active_id : active_) {
    if (id == active_id) {
      return true;
    }
  }

  return false;
}

size_t ActiveNotifications::Size() const {
  MutexLock l(&lock_);
  return active_.size();
}

}  // namespace lmctfy
}  // namespace containers
