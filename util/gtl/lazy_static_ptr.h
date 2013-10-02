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

#ifndef UTIL_GTL_LAZY_STATIC_PTR_H__
#define UTIL_GTL_LAZY_STATIC_PTR_H__

#include "base/macros.h"
#include "base/mutex.h"

namespace util {
namespace gtl {

// Lazily allocates an object of the specified type.
template <typename T>
class LazyStaticPtr {
 public:
  LazyStaticPtr() : ptr_(nullptr) {}

  T &operator*() const { return *get(); }
  T *operator->() const { return get(); }
  T *get() const {
    MutexLock l(&lock_);
    if (ptr_ == nullptr) {
      ptr_ = new T();
    }
    return ptr_;
  }

 private:
  mutable Mutex lock_;
  mutable T *ptr_;

  DISALLOW_COPY_AND_ASSIGN(LazyStaticPtr);
};

}  // namespace gtl
}  // namespace util

#endif  // UTIL_GTL_LAZY_STATIC_PTR_H__
