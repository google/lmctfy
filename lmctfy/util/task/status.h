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

#ifndef UTIL_TASK_STATUS_H__
#define UTIL_TASK_STATUS_H__

#include <string>

#include "base/macros.h"
#include "strings/stringpiece.h"
#include "util/task/codes.pb.h"

namespace util {

// A Status is a combination of an error code and a string message (for non-OK
// error codes).
class Status {
 public:
  // Creates an OK status
  Status();

  // Make a Status from the specified error and message.
  Status(::util::error::Code error, StringPiece error_message);

  Status(const Status &other);
  Status &operator=(const Status &other);

  // Some pre-defined Status objects
  static const Status& OK;             // Identical to 0-arg constructor
  static const Status& CANCELLED;
  static const Status& UNKNOWN;

  // Accessors
  bool ok() const { return code_ == ::util::error::OK; }
  int error_code() const {return code_; }
  ::util::error::Code CanonicalCode() const { return code_; }
  const ::std::string& error_message() const { return message_; }

  bool operator==(const Status& x) const;
  bool operator!=(const Status& x) const;

  // NoOp
  void IgnoreError() const {}

  ::std::string ToString() const;

 private:
  ::util::error::Code code_;
  ::std::string message_;
};

inline bool Status::operator==(const Status &other) const {
  return (this->code_ == other.code_) && (this->message_ == other.message_);
}

inline bool Status::operator!=(const Status &other) const {
  return !(*this == other);
}

extern ::std::ostream& operator<<(::std::ostream& os, const Status& other);

}  // namespace util

#endif  // UTIL_TASK_STATUS_H__
