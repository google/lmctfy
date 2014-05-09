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

// Author: vishnuk@google.com (Vishnu Kannan)
//
// A simple class that is designed to abstract linux mount APIs. Linux mount
// interface is heavily overloaded and ugly. Hence it makes sense to contain all
// mount magic in one class.

#ifndef GLOBAL_UTILS_MOUNT_UTILS_H_
#define GLOBAL_UTILS_MOUNT_UTILS_H_

#include <set>
#include <string>
#include <vector>

#include "base/macros.h"
#include "util/safe_types/bytes.h"
#include "util/proc_mounts.h"
#include "util/task/statusor.h"

namespace util {

class MountUtils {
 public:
  enum Mode {
    MODE_RW,
    MODE_RO
  };

  typedef ::util::ProcMountsData MountObject;

  virtual ~MountUtils() {}

  // The following methods are marked 'virtual' so that tests can override them.

  enum BindMountOpts {
    RECURSIVE,
    READONLY,
    // Either PRIVATE or SLAVE can be specified.
    PRIVATE,
    SLAVE,
  };
  // Bind Mounts 'source' at 'target'. 'source' and 'target' must be of the same
  // file type - directory or file. If 'opts' cointains,
  // READONLY, the 'target' is made readonly;
  // PRIVATE, the 'target' is made a private bind mount; Otherwise it is left as
  // is.
  // RECURSIVE, 'source' is bind mounted recursively at 'target', thereby bring
  // all the sub-mounts under 'source' to 'target'.
  // By default, all the mounts are marked no-suid and no-dev.
  // Returns INTERNAL on any syscall failure.
  virtual ::util::Status BindMount(
      const string &source,
      const string &target,
      const ::std::set<BindMountOpts> &opts) const = 0;

  // Returns a MountObject that represents the most recent mount at
  // 'mountpoint'. Returns NOT_FOUND if no mount is found. Returns INTERNAL if
  // there are is issue with opening or processing '/proc/mounts'.
  virtual ::util::StatusOr<MountObject> GetMountInfo(
      const string &mountpoint) const = 0;

  // Mounts 'device_file' at 'mountpoint' as an ext4 fs. The 'device_file' must
  // be a block device file and the 'mountpoint' should be a directory. If mode
  // is set to 'MODE_RO', this method creates a read only mount at
  // 'mountpoint'. If mode is set to 'MODE_RW', this method creates a writable
  // mount at 'mountpoint'. Returns INTERNAL if mount fails.
  virtual ::util::Status MountDevice(const string &device_file,
                                     const string &mountpoint,
                                     const Mode mode) const = 0;

  // Mounts a tmpfs filesystem at 'visible_at'. The maximum size of the fs will
  // be set to 'size_bytes'. Any mount options specified in 'mount_opts' will be
  // applied to the tmpfs mount. Do not specify size as a mount option. If a
  // tmpfs mount already exists at 'visible_at' a remount will happen. `man
  // mount` will list the mount options relevant to tmpfs. Returns error if
  // 'visible_at' is invalid or 'size_bytes' is zero or a non-tmpfs mount
  // already exists at 'visible_at' or if the mount fails for any other reason.
  virtual ::util::Status MountTmpfs(
      const string &visible_at, const Bytes size_bytes,
      const ::std::vector<string> &mount_opts) const = 0;

  // Unmounts the mount at 'mountpoint'. Returns OK if no mount exists at
  // 'mountpoint'. Returns INTERNAL on unmount failure.
  virtual ::util::Status Unmount(const string &mountpoint) const = 0;

  // Unmounts all the mountpoints under given dir (including the dir itself).
  // Returns INVALID_ARGUMENT if dir_path is empty or not absolute path.
  // Returns INTERNAL on unmount failure.
  // Returns Status::OK if we succeeded in unmounting dir_path OR if dir_path
  // did not exist OR if dir_path was not a mount-point.
  virtual ::util::Status UnmountRecursive(const string &dir_path) const = 0;

 protected:
  MountUtils() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(MountUtils);
};

// Returns a singleton instance of the GlobalMountUtils interface
// implementation.
const MountUtils *GlobalMountUtils();

}  // namespace util

#endif  // GLOBAL_UTILS_MOUNT_UTILS_H_
