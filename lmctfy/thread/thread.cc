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

#include "thread/thread.h"

#include "base/casts.h"
#include "base/logging.h"

using ::std::string;

void *Thread::ThreadMain(void *arg) {
  Thread *thread = static_cast<Thread *>(arg);
  thread->Run();
  return nullptr;
}

Thread::Thread() : running_(false) {}
Thread::~Thread() {}

void Thread::Start() {
  CHECK(!running_);
  pthread_create(&thread_, nullptr, &ThreadMain, this);
  running_ = true;

  // Detach the thread if it is not joinable.
  if (!options_.joinable()) {
    pthread_detach(thread_);
  }
}

void Thread::Join() {
  CHECK(running_);
  CHECK(options_.joinable());

  void *unused;
  pthread_join(thread_, &unused);
  running_ = false;
}

void Thread::SetJoinable(bool joinable) {
  CHECK(!running_) << "Can't SetJoinable() on a running thread";
  options_.set_joinable(joinable);
}

void Thread::SetNamePrefix(const string& name_prefix) {
  CHECK(!running_) << "Can't SetNamePrefix() on a running thread";
}

ClosureThread::ClosureThread(Closure *closure) : closure_(closure) {
  CHECK(closure_->IsRepeatable())
      << "Must use a permanent callback for a ClosureThread";
}

ClosureThread::ClosureThread(const ::thread::Options& options,
                             const string& name_prefix,
                             Closure* closure)
    : closure_(closure) {
  options_ = options;
  SetNamePrefix(name_prefix);
  CHECK(closure_->IsRepeatable())
      << "Must use a permanent callback for a ClosureThread";
}

ClosureThread::~ClosureThread() {}

void ClosureThread::Run() {
  closure_->Run();
}
