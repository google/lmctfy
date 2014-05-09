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

#ifndef BASE_NOTIFICATION_H__
#define BASE_NOTIFICATION_H__

#include <pthread.h>

// Simple class that waits for a single notification
//
// Class is thread-safe.
class Notification {
 public:
  Notification();
  ~Notification();

  void Notify();

  // Block until a notification is received.
  void WaitForNotification() const;

 private:
  bool notified_;
  mutable pthread_mutex_t mutex_;
  mutable pthread_cond_t cond_;
};

#endif  // BASE_NOTIFICATION_H__
