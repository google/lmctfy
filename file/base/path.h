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

#ifndef FILE_BASE_PATH_H__
#define FILE_BASE_PATH_H__

#include "strings/stringpiece.h"

namespace file {

// Joins the specified path elements.
// e.g.:
// JoinPath("/", "foo") -> "/bar"
// JoinPath("/foo", "bar") -> "/foo/bar"
// JoinPath("/foo/", "/bar") -> "/foo/bar"
// JoinPath("/foo", "") -> "/foo"

string JoinPath(StringPiece p1, StringPiece p2, StringPiece p3);

inline string JoinPath(StringPiece p1, StringPiece p2) {
  return JoinPath(p1, p2, "");
}

// Return the "basename" for "fname".  I.e. strip out everything up to and
// including the last "/" in the name.
StringPiece Basename(StringPiece path);

}  // namespace file

#endif  // FILE_BASE_PATH_H__
