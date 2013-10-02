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

#include "file/util/linux_fileops.h"

#include <dirent.h>
#include <errno.h>
#include <stddef.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <algorithm>

#include "base/casts.h"
#include "base/logging.h"
#include "file/base/path.h"
#include "strings/substitute.h"

using ::file::JoinPath;
using ::std::string;
using ::std::vector;
using ::strings::Substitute;

namespace file_util {

bool LinuxFileOps::ListDirectorySubdirs(const string &directory,
                                        vector<string> *entries,
                                        bool fully_resolve,
                                        string* error) {
  bool success = true;
  DIR* dir = opendir(directory.c_str());

  if (dir == NULL) {
    *error = Substitute("opendir failed on $0", directory);
    return false;
  }

  // Iterate through directory entries
  errno = 0;
  struct dirent64 dirent_buffer;
  struct dirent64 *dirent = NULL;
  int ret;
  while (((ret = readdir64_r(dir, &dirent_buffer, &dirent)) == 0) && dirent) {
    // Skip "." or ".."
    if (dirent->d_name[0] == '.' &&
        (dirent->d_name[1] == '\0' ||
         (dirent->d_name[1] == '.' && dirent->d_name[2] == '\0'))) {
      continue;
    }

    // Add directories to output.
    string filename = JoinPath(directory, dirent->d_name);
    struct stat64 st;
    if (lstat64(filename.c_str(), &st) != 0) {
      success = false;
      *error = Substitute("lstat failed on $0", filename);
      break;
    }
    if (S_ISDIR(st.st_mode)) {
      entries->push_back(dirent->d_name);
    }
  }

  if ((ret != 0) && error->empty()) {
    success = false;
    *error = Substitute("readdir failed on $0", directory);
  }

  if (closedir(dir) != 0) {
    LOG(WARNING) << Substitute("closedir failed on $0 with errno $1",
                               directory,
                               errno);
  }

  return success;
}

}  // namespace file_util
