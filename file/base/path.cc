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

#include "file/base/cleanpath.h"
#include "strings/join.h"

using ::std::make_pair;
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

string JoinPath(StringPiece p1, StringPiece p2, StringPiece p3,
                StringPiece p4) {
  string result = p1.ToString();

  AppendPath(&result, p2);
  AppendPath(&result, p3);
  AppendPath(&result, p4);

  return result;
}

namespace internal {

// Return the parts of the path, split on the final "/".  If there is no
// "/" in the path, the first part of the output is empty and the second
// is the input. If the only "/" in the path is the first character, it is
// the first part of the output.
std::pair<StringPiece, StringPiece> SplitPath(StringPiece path) {
  stringpiece_ssize_type pos = path.find_last_of('/');

  // Handle the case with no '/' in 'path'.
  if (pos == StringPiece::npos) {
    return make_pair(StringPiece(path, 0, 0), path);
  }

  // Handle the case with a single leading '/' in 'path'.
  if (pos == 0) {
    return make_pair(StringPiece(path, 0, 1), StringPiece(path, 1));
  }

  return make_pair(StringPiece(path, 0, pos), StringPiece(path, pos + 1));
}

}  // namespace internal

StringPiece Dirname(StringPiece path) {
  return internal::SplitPath(path).first;
}

StringPiece Basename(StringPiece path) {
  return internal::SplitPath(path).second;
}

bool IsAbsolutePath(StringPiece path) {
  return !path.empty() && path[0] == '/';
}

string AddSlash(StringPiece path) {
  int length = path.length();
  if (length && path[length - 1] != '/') {
    return StrCat(path, "/");
  } else {
    return path.ToString();
  }
}

string CleanPath(StringPiece path) {
  return Plan9_CleanPath(path.ToString());
}

}  // namespace file
