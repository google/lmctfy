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

#include "global_utils/fs_utils.h"

#include <errno.h>

#include <string>

#include "strings/substitute.h"
#include "system_api/libc_fs_api.h"
#include "util/errors.h"
#include "util/task/codes.pb.h"
#include "util/task/statusor.h"

using ::strings::Substitute;
using ::system_api::GlobalLibcFsApi;
using ::util::error::INVALID_ARGUMENT;
using ::util::error::INTERNAL;
using ::util::error::NOT_FOUND;
using ::util::Status;
using ::util::StatusOr;

namespace util {

namespace {

static bool IsDirectory(const struct stat &statbuf) {
  return S_ISDIR(statbuf.st_mode);
}

static bool IsRegFile(const struct stat &statbuf) {
  return S_ISREG(statbuf.st_mode);
}

class FsUtilsImpl : public FsUtils {
 public:
  FsUtilsImpl() {}

  virtual ~FsUtilsImpl() {}

  Status SafeEnsureDir(const string &dirpath,
                       const mode_t mode) const override {
    if (mode == 0) {
      return {INVALID_ARGUMENT, "Mode is invalid"};
    }
    if (dirpath.empty()) {
      return {INVALID_ARGUMENT, "dirpath is empty"};
    }
    return SafeEnsureDirInternal(dirpath, mode);
  }

  Status DirExists(const string &dirpath) const override {
    struct stat statbuf;
    if (GlobalLibcFsApi()->LStat(dirpath.c_str(), &statbuf) == -1) {
      if (errno != ENOENT) {
        return {INTERNAL, Substitute("Unable to LStat $0. Error: $1",
                                     dirpath, strerror(errno))};
      }
      return {NOT_FOUND, Substitute("$0 is not found in the filesystem",
                                    dirpath)};
    }
    if (!IsDirectory(statbuf)) {
      return {INVALID_ARGUMENT, Substitute("$0 is not a directory", dirpath)};
    }
    return Status::OK;
  }

  StatusOr<bool> FileExists(const string &filepath) const override {
    struct stat statbuf;
    if (GlobalLibcFsApi()->LStat(filepath.c_str(), &statbuf) == -1) {
      if (errno != ENOENT) {
        return Status(INTERNAL, Substitute("Unable to LStat $0. Error: $1",
                                           filepath, strerror(errno)));
      }
      return false;
    }
    return true;
  }

 private:
  // Creates a new directory at 'dirpath'. Returns INTERNAL if creating
  // directory fails for any reason including a directory already existing at
  // dirpath. 'mode' will be the mode applied to 'dirpath'.
  Status MkDir(const char dirpath[], mode_t mode) const {
    if (GlobalLibcFsApi()->MkDir(dirpath, mode) == 0) {
      return Status::OK;
    }
    return {INTERNAL, Substitute("Cannot mkdir $0. Error: $1", dirpath,
                                 strerror(errno))};
  }

  // Sets the mode on 'path' to point to 'mode'. 'statbuf' must contain most
  // recent stat information about 'path'. If 'path' points at a regular file,
  // the execute bit is not set on it. Setgid bit on 'path' is preserved.
  // Returns INTERNAL if syscall fails.
  Status SafeSetMode(const char path[],
                     mode_t mode,
                     const struct stat &statbuf) const {
    if (IsRegFile(statbuf)) {
      // Don't set execute bit on files
      mode &= 0666;
    }
    // Don't lose the setgid permission bit
    if (statbuf.st_mode & S_ISGID) {
      mode |= S_ISGID;
    }
    mode_t current_perms = statbuf.st_mode & 07777;
    if (current_perms != mode) {
      if (GlobalLibcFsApi()->ChMod(path, mode)) {
        return Status(INTERNAL, Substitute("Failed to chmod $0. Error: $1",
                                           path, strerror(errno)));
      }
    }
    return Status::OK;
  }

  Status SafeEnsureDirInternal(const string &dirpath,
                               const mode_t mode) const {
    Status result = DirExists(dirpath);
    if (!result.ok()) {
      if (result.error_code() == NOT_FOUND) {
        RETURN_IF_ERROR(MkDir(dirpath.c_str(), mode));
      } else {
        // Either 'dirpath' points to something other than a directory or
        // syscall failed.
        return result;
      }
    }
    // Directory exists. Set permissions on it.
    struct stat statbuf;
    {
      if (GlobalLibcFsApi()->Stat(dirpath.c_str(), &statbuf) == -1) {
        return {INTERNAL, Substitute("Unable to Stat $0. Error: $1",
                                     dirpath, strerror(errno))};
      }
    }
    RETURN_IF_ERROR(SafeSetMode(dirpath.c_str(), mode, statbuf));
    return Status::OK;
  }



  DISALLOW_COPY_AND_ASSIGN(FsUtilsImpl);
};

}  // namespace

const FsUtils *GlobalFsUtils() {
  static FsUtils *util = new FsUtilsImpl();
  return util;
}

}  // namespace util
