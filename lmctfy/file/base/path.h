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

string JoinPath(StringPiece p1, StringPiece p2, StringPiece p3, StringPiece p4);

inline string JoinPath(StringPiece p1, StringPiece p2, StringPiece p3) {
  return JoinPath(p1, p2, p3, "");
}

inline string JoinPath(StringPiece p1, StringPiece p2) {
  return JoinPath(p1, p2, "", "");
}

// Returns the part of the path before the final "/".  If there is a single
// // leading "/" in the path, the result will be the leading "/".  If there is
// // no "/" in the path, the result is the empty prefix of the input.
StringPiece Dirname(StringPiece path);

// Return the "basename" for "fname".  I.e. strip out everything up to and
// including the last "/" in the name.
StringPiece Basename(StringPiece path);

// Return true if path is absolute.
bool IsAbsolutePath(StringPiece path);

// If path is non-empty and doesn't already end with a slash, append oneto the
// end.
string AddSlash(StringPiece path);

// Collapse duplicate "/"s, resolve ".." and "." path elements, remove trailing
// "/".
//
// NOTE: This respects relative vs. absolute paths, but does not invoke any
// system calls (getcwd(2)) in order to resolve relative paths wrt actual
// working directory.  That is, this is purely a string manipulation, completely
// independent of process state.
string CleanPath(StringPiece path);

}  // namespace file

#endif  // FILE_BASE_PATH_H__
