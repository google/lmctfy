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

#ifndef THREAD_THREAD_H__
#define THREAD_THREAD_H__

#include <pthread.h>
#include <memory>
#include <string>

#include "base/callback.h"
#include "thread/thread_options.h"

// Class is thread-compatible.
class Thread {
 public:
  Thread();
  virtual ~Thread();
  void Start();
  void Join();
  void SetJoinable(bool joinable);
  void SetNamePrefix(const ::std::string &name_prefix);
  const ::thread::Options& options() const { return options_; }

 protected:
  static void *ThreadMain(void *arg);
  virtual void Run() = 0;

  ::thread::Options options_;

 private:
  pthread_t thread_;
  bool running_;
};

// Class is thread-compatible.
class ClosureThread : public Thread {
 public:
  // Takes ownership of closure.
  explicit ClosureThread(Closure *closure);
  ClosureThread(const ::thread::Options& options,
                const ::std::string& name_prefix,
                Closure* closure);
  ~ClosureThread();

 protected:
  virtual void Run();

 private:
  ::std::unique_ptr<Closure> closure_;
};

#endif  // THREAD_THREAD_H__
