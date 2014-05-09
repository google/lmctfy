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

//
// NsHandle implementation.
//

#include "nscon/ns_handle.h"

#include <vector>

#include "file/base/path.h"
#include "util/errors.h"
#include "util/file_lines.h"
#include "strings/numbers.h"
#include "strings/split.h"
#include "strings/substitute.h"

using ::util::FileLines;
using ::file::JoinPath;
using ::std::vector;
using ::strings::Split;
using ::strings::Substitute;
using ::util::Status;
using ::util::StatusOr;
using ::util::error::INTERNAL;

namespace containers {
namespace nscon {

StatusOr<string> CookieGenerator::GenerateCookie(pid_t pid) const {
  const string file_path = JoinPath("/proc", SimpleItoa(pid), "stat");

  // We're only interested in the first (and only) line of the file.
  for (const StringPiece line : FileLines(file_path)) {
    // The 21st field in /proc/<pid>/stat file is the start time of the process.
    static const int kStartTimeField = 21;
    const vector<string> fields = Split(line, " ");
    if (fields.size() < kStartTimeField + 1) {
      return Status(INTERNAL, Substitute(
                                  "Unexpected contents in file \"$0\": \"$1\" "
                                  "while generating cookie",
                                  file_path, line));
    }

    return Substitute("c$0", fields[kStartTimeField]);
  }

  return Status(
      INTERNAL,
      Substitute("Failed to read contents of \"$0\" while generating cookie",
                 file_path));
}

StatusOr<NsHandleFactory *> NsHandleFactory::New() {
  return new NsHandleFactory(new CookieGenerator());
}

StatusOr<const NsHandle *> NsHandleFactory::Get(pid_t pid) const {
  string cookie = RETURN_IF_ERROR(cookie_generator_->GenerateCookie(pid));

  return new NsHandle(pid, cookie, cookie_generator_.get());
}

StatusOr<const NsHandle *> NsHandleFactory::Get(const string &handlestr) const {
  vector<string> tokens = Split(handlestr, "-");

  if (tokens.size() != 2) {
    return Status(::util::error::INVALID_ARGUMENT,
                  Substitute("Malformed handlestr \"$0\"", handlestr));
  }

  pid_t pid;
  if (!SimpleAtoi(tokens[1], &pid)) {
    return Status(::util::error::INVALID_ARGUMENT,
                  Substitute("Malformed handlestr \"$0\" invalid PID \"$1\"",
                             handlestr, tokens[1]));
  }

  string cookie = RETURN_IF_ERROR(cookie_generator_->GenerateCookie(pid));
  if (tokens[0] != cookie) {
    return Status(::util::error::INVALID_ARGUMENT,
                  Substitute("Stale nshandle \"$0\"", tokens[0]));
  }

  return new NsHandle(pid, cookie, cookie_generator_.get());
}

bool NsHandle::IsValid() const {
  StatusOr<string> statusor = cookie_generator_->GenerateCookie(base_pid_);
  if (!statusor.ok()) {
    return false;
  }

  return statusor.ValueOrDie() == cookie_;
}

const string NsHandle::ToString() const {
  return Substitute("$0-$1", cookie_, base_pid_);
}

pid_t NsHandle::ToPid() const {
  return base_pid_;
}

}  // namespace nscon
}  // namespace containers
