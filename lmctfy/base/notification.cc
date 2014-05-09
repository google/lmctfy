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

#include "base/notification.h"

#include "base/logging.h"

Notification::Notification() : notified_(false) {
  CHECK(pthread_mutex_init(&mutex_, nullptr) == 0);
  CHECK(pthread_cond_init(&cond_, nullptr) == 0);
}

Notification::~Notification() {
  pthread_mutex_destroy(&mutex_);
  pthread_cond_destroy(&cond_);
}

void Notification::Notify() {
  CHECK(pthread_mutex_lock(&mutex_) == 0);
  notified_ = true;
  pthread_cond_broadcast(&cond_);
  CHECK(pthread_mutex_unlock(&mutex_) == 0);
}

  // Block until you are notified.
void Notification::WaitForNotification() const {
  CHECK(pthread_mutex_lock(&mutex_) == 0);

  // Wait if we have not already been notified.
  if (!notified_) {
    pthread_cond_wait(&cond_, &mutex_);
  }

  CHECK(pthread_mutex_unlock(&mutex_) == 0);
}
