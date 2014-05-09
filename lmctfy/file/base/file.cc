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

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "file/base/file.h"

#include "file/base/path.h"

namespace file {

bool IsAbsolutePath(const string &path) {
  return !path.empty() && (path[0] == '/');
}

}  // namespace file

string File::Basename(const string& fname) {
  size_t stop = fname.back() == '/' ? fname.size() - 2 : string::npos;
  size_t start = fname.find_last_of('/', stop);

  // If no slash, just return.
  if (start == string::npos) {
    return fname;
  }

  return fname.substr(start + 1, stop - start);
}

string File::StripBasename(const string &fname) {
  size_t last_slash = fname.find_last_of('/');

  // If no slash trim all. If already root (or close to it), return that.
  if (last_slash == string::npos) {
    return "";
  } else if (fname == "/" || last_slash == 0) {
    return "/";
  }

  return fname.substr(0, last_slash);
}

bool File::Exists(const string &path) {
  struct stat buf;
  return stat(path.c_str(), &buf) == 0;
}

bool File::Delete(const string &path) {
  return remove(path.c_str()) == 0;
}
