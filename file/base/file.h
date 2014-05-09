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

#ifndef FILE_BASE_FILE_H__
#define FILE_BASE_FILE_H__

#include <string>
using ::std::string;

namespace file {
bool IsAbsolutePath(const string &path);

}  // namespace file

class File {
 public:
  // Return the "basename" for "fname".  I.e. strip out everything
  // up to and including the last "/" in the name.
  static string Basename(const string& fname);

  static string StripBasename(const string &fname);

  static bool Exists(const string &path);

  static bool Delete(const string &path);
};

#endif  // FILE_BASE_FILE_H__
