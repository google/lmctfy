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

#ifndef THREAD_THREAD_OPTIONS_H__
#define THREAD_THREAD_OPTIONS_H__

namespace thread {

class Options {
 public:
  Options() : joinable_(false) {}

  Options& set_joinable(bool joinable) { joinable_ = joinable; return *this; }

  bool joinable() const { return joinable_; }

 private:
  bool joinable_;
};

}  // namespace thread

#endif  // THREAD_THREAD_OPTIONS_H__
