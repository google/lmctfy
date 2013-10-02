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

#ifndef UTIL_SCOPED_CLEANUP_H_
#define UTIL_SCOPED_CLEANUP_H_

namespace util {

// A scoped cleanup action that is performed on destruction. The action can be
// cancelled by calling release(). The ScopedCleanup makes a copy of the
// specified ValueType and holds it internally.
//
// Sample cleanup action class (by convention prefer imperative names like:
// Close, Delete, Unlink):
//
// class Close {
//  public:
//   typedef int ValueType;
//
//   static void Cleanup(const int &value) {
//     close(value);
//   }
// };
//
// This is used in the following pattern:
//
// int GetFD() {
//   ScopedCleanup<Close> c(open(file));
//
//   // Initialize file.
//   if (!Init(c.get())) {
//     return -1;
//   }
//
//   // More work with failures.
//
//   return c.release();
// }
//
// Class is thread-compatible.
template <typename T>
class ScopedCleanup {
 public:
  typedef typename T::ValueType ValueType;

  // Creates a scoped cleanup of the specified value.
  explicit ScopedCleanup(const ValueType &value)
      : released_(false), value_(value) {}

  ~ScopedCleanup() {
    // Run the action if it has not been released
    if (!released_) {
      T::Cleanup(value_);
    }
  }

  // Releases the underlying value. The value will not be cleaned up on
  // destruction.
  ValueType release() {
    released_ = true;
    return value_;
  }

  // Gets the underlying value.
  const ValueType &get() const {
    return value_;
  }

 private:
  // Whether the value has been released from cleanup.
  bool released_;

  // The value to be cleaned up.
  ValueType value_;
};

}  // namespace util

#endif  // UTIL_SCOPED_CLEANUP_H_
