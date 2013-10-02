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

#include "file/base/path.h"

#include <string>

using ::std::string;

namespace file {

namespace {

// Append to_append to path.
void AppendPath(string *path, StringPiece to_append) {
  // Check if nothing to append.
  if (to_append.empty()) {
    return;
  }

  if (path->back() == '/') {
    if (to_append[0] == '/') {
      StringPiece tmp = to_append.substr(1);
      path->append(tmp.data(), tmp.size());
    } else {
      path->append(to_append.data(), to_append.size());
    }
  } else {
    if (to_append[0] == '/') {
      path->append(to_append.data(), to_append.size());
    } else {
      path->append("/");
      path->append(to_append.data(), to_append.size());
    }
  }
}

}  // namespace

string JoinPath(StringPiece p1, StringPiece p2, StringPiece p3) {
  string result = p1.ToString();

  AppendPath(&result, p2);
  AppendPath(&result, p3);

  return result;
}

}  // namespace file
