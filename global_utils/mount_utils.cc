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

#include "global_utils/mount_utils.h"

#include <errno.h>
#include <string.h>
#include <linux/fs.h>

#include <set>
#include <string>
#include <vector>

#include "base/integral_types.h"
#include "base/logging.h"
#include "base/macros.h"
#include "file/base/path.h"
#include "strings/join.h"
#include "strings/strip.h"
#include "strings/substitute.h"
#include "system_api/libc_fs_api.h"
#include "util/proc_mounts.h"
#include "util/safe_types/bytes.h"
#include "util/task/codes.pb.h"
#include "util/task/statusor.h"

using ::strings::Substitute;
using ::system_api::GlobalLibcFsApi;
using ::util::Status;
using ::util::StatusOr;
using ::util::error::FAILED_PRECONDITION;
using ::util::error::INTERNAL;
using ::util::error::INVALID_ARGUMENT;
using ::util::error::NOT_FOUND;

namespace util {

namespace {

static const uint64 kMountFlags = MS_NODEV | MS_NOSUID;

class MountUtilsImpl : public MountUtils {
 public:
  MountUtilsImpl() {}

  Status BindMount(const string &source, const string &target,
                   const ::std::set<BindMountOpts> &opts) const override {
    if (opts.find(PRIVATE) != opts.end() && opts.find(SLAVE) != opts.end()) {
      return Status(INVALID_ARGUMENT,
                    "Specify either PRIVATE or SLAVE as mount options");
    }
    uint64 mount_flags = kMountFlags | MS_BIND;
    if (opts.find(RECURSIVE) != opts.end()) {
      mount_flags |= MS_REC;
    }
    if (GlobalLibcFsApi()->Mount(source.c_str(), target.c_str(), "",
                                 mount_flags, nullptr) == -1) {
      return Status(INTERNAL,
                    Substitute("Could not bind mount $0 at $1 with flags: "
                               "$2. Error: $3", source, target, mount_flags,
                               strerror(errno)));
    }
    if (opts.find(READONLY) != opts.end()) {
      mount_flags |= (MS_REMOUNT | MS_RDONLY);
      if (GlobalLibcFsApi()->Mount(source.c_str(), target.c_str(), "",
                                   mount_flags, nullptr) == -1) {
        return Status(INTERNAL,
                      Substitute("Could not remount as readonly the bind mount"
                                 " at $0 with flags: $1. Error: $2", target,
                                 mount_flags, strerror(errno)));
      }
    }
    if (opts.find(PRIVATE) != opts.end()) {
      mount_flags = MS_PRIVATE;
      if (opts.find(RECURSIVE) != opts.end()) {
        mount_flags |= MS_REC;
      }
      if (GlobalLibcFsApi()->Mount(source.c_str(), target.c_str(), "",
                                   mount_flags, nullptr) == -1) {
        return Status(INTERNAL,
                      Substitute("Could not mark as private the bind mount"
                                 " at $0 with flags: $1. Error: $2", target,
                                 mount_flags, strerror(errno)));
      }
    } else if (opts.find(SLAVE) != opts.end()) {
      mount_flags = MS_SLAVE;
      if (opts.find(RECURSIVE) != opts.end()) {
        mount_flags |= MS_REC;
      }
      if (GlobalLibcFsApi()->Mount(source.c_str(), target.c_str(), "",
                                   mount_flags, nullptr) == -1) {
        return Status(INTERNAL,
                      Substitute("Could not mark as slave the bind mount"
                                 " at $0 with flags: $1. Error: $2", target,
                                 mount_flags, strerror(errno)));
      }
    }
    return Status::OK;
  }

  StatusOr<MountObject> GetMountInfo(const string &mountpoint) const override {
    string clean_mountpoint(mountpoint);
    TrimStringRight(&clean_mountpoint, "/");
    // Invalid input.
    if (clean_mountpoint.empty()) {
      return Status(INVALID_ARGUMENT, "mountpoint is empty.");
    }
    bool success = false;
    MountObject mobj;
    for (const MountObject &mount : ProcMounts(0)) {
      // If we find multiple mounts at clean_mountpoint we will choose the most
      // recent mount. In /proc/mounts the mount points are listed in the order
      // in which they were created.
      if (mount.mountpoint == clean_mountpoint) {
        success = true;
        mobj = mount;
      }
    }
    if (!success) {
      return Status(NOT_FOUND,
                    Substitute("$0 does not contain any mount.",
                               clean_mountpoint));
    }
    return mobj;
  }

  Status MountDevice(const string &device_file, const string &mountpoint,
                     const Mode mode) const override {
    uint64 mount_flags =
        (mode == MODE_RO) ? kMountFlags | MS_RDONLY : kMountFlags;
    if (GlobalLibcFsApi()->Mount(device_file.c_str(), mountpoint.c_str(),
                                 "ext4", mount_flags, nullptr) == -1) {
      return Status(INTERNAL,
                    Substitute("Could not mount $0 at $1 with flags: $2. "
                               "Error: $3", device_file, mountpoint,
                               mount_flags, strerror(errno)));
    }
    return Status::OK;
  }

  Status MountTmpfs(const string &visible_at, const Bytes size_bytes,
                    const vector<string> &mount_opts) const override {
    const char kTmpfsType[] = "tmpfs";
    if (visible_at.empty()) {
      return Status(INVALID_ARGUMENT, "visible_at is an empty string");
    }
    if (size_bytes.value() <= 0) {
      return Status(INVALID_ARGUMENT, "Invalid tmpfs size.");
    }

    // Handle possible remount.
    uint64 flags = 0;
    auto prexisting_mount = GetMountInfo(visible_at);
    if (prexisting_mount.ok()) {
      if (prexisting_mount.ValueOrDie().type == kTmpfsType) {
        // A mount already exists at 'visible_at. This mount will be marked as a
        // remount.
        flags |= MS_REMOUNT;
      } else {
        // A non-tmpfs mount already exists at 'visible_at'.
        return Status(FAILED_PRECONDITION, Substitute(
            "A non-tmpfs mount already exists at $0", visible_at));
      }
    }

    vector<string> local_mount_opts;
    for (const string &opt : mount_opts) {
      if (opt.find("size") == string::npos)
        local_mount_opts.push_back(opt);
    }

    // Handle tmpfs size. Append the size option to the current mount options.
    local_mount_opts.push_back(Substitute("size=$0", size_bytes.value()));

    string mount_opts_str = strings::Join(local_mount_opts, ",");

    // Do the actual mounting of tmpfs.
    if (GlobalLibcFsApi()->Mount(kTmpfsType, visible_at.c_str(), kTmpfsType,
                                 flags, mount_opts_str.c_str())) {
      return Status(INTERNAL, Substitute(
          "Unable to mount tmpfs at $0 with size $1 Bytes and options $2. "
          "Error: $3", visible_at, size_bytes.value(), mount_opts_str,
          strerror(errno)));
    }
    return Status::OK;
  }

  Status Unmount(const string &mountpoint) const override {
    if ((GlobalLibcFsApi()->UMount(mountpoint.c_str()) == -1) &&
        (errno != EINVAL)) {
      return Status(INTERNAL,
                    Substitute("Unable to unmount mount at $0. Error: $1",
                               mountpoint, strerror(errno)));
    }
    return Status::OK;
  }

  Status UnmountRecursive(const string &dir_path) const override {
    if (dir_path.empty()) {
      return Status(INVALID_ARGUMENT, "Specified path is empty");
    }

    if (!file::IsAbsolutePath(dir_path)) {
      return Status(INVALID_ARGUMENT,
                    Substitute("Must specify absolute path: $0", dir_path));
    }

    // Make sure the path ends with '/'. This is so that we match only directory
    // mountpoints.
    const string path(file::AddSlash(dir_path));
    vector<string> mountpoints = GetMountsWithPrefix(path);

    // Unmount all the matching mountpoints.
    bool success = true;
    vector <string> err_mounts;
    while (!mountpoints.empty()) {
      const string &mount_point = mountpoints.back();
      if (GlobalLibcFsApi()->UMount(mount_point.c_str()) != 0) {
        err_mounts.emplace_back(mount_point);
        success = false;
      }
      mountpoints.pop_back();
    }

    // Unmount the path itself if it's not already been unmounted during the
    // loop above.
    if ((GlobalLibcFsApi()->UMount(path.c_str()) == 0) ||
        (errno == ENOENT) || (errno == EINVAL)) {
      // We successfully unmounted this directory or it doesn't exist.
      return Status::OK;
    } else if ((errno == EBUSY) && !success) {
      // We failed to unmount some sub-tree.
      return Status(INTERNAL,
                    Substitute("Failed to unmount some paths: $0",
                               strings::Join(err_mounts, ",")));
    }

    return Status(INTERNAL,
                  Substitute("umount($0) failed: $1", path, strerror(errno)));
  }

 protected:
  vector<string> GetMountsWithPrefix(const string &prefix) const {
    // Read /proc/self/mounts and create a list of mountpoints that match the
    // given prefix.
    vector<string> mountpoints;
    for (const ProcMountsData &mount : ProcMounts(0)) {
      if (mount.mountpoint.find(prefix) == 0) {
        // Deleted directories have '\t(deleted)' suffix to the entries in
        // /proc/mounts. Remove that suffix if present.
        size_t pos = mount.mountpoint.find("\t(deleted)");
        if (pos != string::npos) {
          mountpoints.emplace_back(mount.mountpoint.substr(0, pos));
        } else {
          mountpoints.emplace_back(mount.mountpoint);
        }
      }
    }
    return mountpoints;
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(MountUtilsImpl);
};

}  // namespace

const MountUtils *GlobalMountUtils() {
  static MountUtils *util = new MountUtilsImpl();
  return util;
}

}  // namespace util
