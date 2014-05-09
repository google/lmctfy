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

#ifndef GLOBAL_UTILS_FS_UTILS_H_
#define GLOBAL_UTILS_FS_UTILS_H_

#include <string>

#include "util/task/statusor.h"

namespace util {

class FsUtils {
 public:
  virtual ~FsUtils() {}

  // Creates a directory at 'dirpath', if one doesn't already exist. The mode on
  // the directory is set to match 'mode'.
  // If 'dirpath' is created its uid will be equal to that of the effective uid
  // of the calling process. The gid will equal that of the calling process if
  // parent dir doesn't have the set-gid bit, or the gid on the parent dir if it
  // does. If 'dirpath' already exists, its ownership is left unchanged.
  // Returns INVALID_ARGUMENT if 'dirpath' points to a something other than a
  // directory or if mode is 0. Returns INTERNAL if any syscall fails.
  // Does not undo any steps if a failure is encountered, the caller is expected
  // to unlink 'dirpath' on failure.
  virtual ::util::Status SafeEnsureDir(const string &dirpath,
                                       mode_t mode) const = 0;

  // Returns OK if a directory exists at 'dirpath'. Returns NOT_FOUND if
  // 'dirpath' doesn't exist. Returns INVALID_ARGUMENT if 'dirpath' points to
  // something other than a directory. Returns INTERNAL if syscall fails.
  virtual ::util::Status DirExists(const string &dirpath) const = 0;

  // Returns true if a file exists at 'filepath'. Returns false if
  // 'filepath' doesn't exist. Returns INVALID_ARGUMENT if 'filepath' points to
  // something other than a file. Returns INTERNAL if syscall fails.
  virtual ::util::StatusOr<bool> FileExists(
      const ::std::string &filepath) const = 0;

 protected:
  FsUtils() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(FsUtils);
};

// Returns a singleton instance of the GlobalFsUtils interface
// implementation.
const FsUtils *GlobalFsUtils();

}  // namespace util

#endif  // GLOBAL_UTILS_FS_UTILS_H_
