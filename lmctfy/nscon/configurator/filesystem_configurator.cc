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

//
// FilesystemConfigurator implementation.
//
#include "nscon/configurator/filesystem_configurator.h"

#include <sys/mount.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include "gflags/gflags.h"
#include "file/base/path.h"
#include "include/namespaces.pb.h"
#include "global_utils/fs_utils.h"
#include "global_utils/mount_utils.h"
#include "global_utils/time_utils.h"
#include "util/errors.h"
#include "util/proc_mounts.h"
#include "system_api/libc_fs_api.h"
#include "strings/substitute.h"
#include "util/task/status.h"

using ::system_api::GlobalLibcFsApi;
using ::util::GlobalFsUtils;
using ::util::GlobalMountUtils;
using ::util::GlobalTimeUtils;
using ::util::MountUtils;
using ::util::ProcMountsData;
using ::util::ScopedCleanup;
using ::std::set;
using ::std::vector;
using ::strings::Substitute;
using ::util::error::INTERNAL;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace nscon {

const char *FilesystemConfigurator::kFsRoot = "/";
const char *FilesystemConfigurator::kDefaultProcfsPath = "/proc/";
const char *FilesystemConfigurator::kDefaultSysfsPath = "/sys/";
const int FilesystemConfigurator::kDefaultMountFlags =
    (MS_NODEV | MS_NOEXEC | MS_NOSUID | MS_RELATIME);
const char *FilesystemConfigurator::kDefaultDevptsPath = "/dev/pts";
const char *FilesystemConfigurator::kDevptmxPath = "/dev/ptmx";
const char *FilesystemConfigurator::kDevptsMountData =
    "newinstance,ptmxmode=0666,mode=620,gid=5";

Status FilesystemConfigurator::PrepareFilesystem(
    const set<string> &whitelisted_mounts,
    const string &rootfs_path) const {
  // chdir() to our new rootfs first because we may unmount our CWD below.
  if (GlobalLibcFsApi()->ChDir(rootfs_path.c_str()) < 0) {
    return Status(INTERNAL,
                  Substitute("chdir($0) failed: $1", rootfs_path,
                             StrError(errno)));
  }

  const string rootfs_dir = file::AddSlash(rootfs_path);
  // Generate the list of mountpoints to unmount (i.e. everything other than "/"
  // and whats under new rootfs).
  vector<string> mountpoints;
  for (const ProcMountsData &mount : ::util::ProcMounts()) {
    // Skip "/".
    if (mount.mountpoint == kFsRoot) {
      continue;
    }

    // Skip all mounts in whitelisted mounts. Also skip mounts that would have
    // been made inaccessible by the whitelisted_mounts. This is required only
    // if no custom rootfs path is specified.
    if (rootfs_dir == kFsRoot) {
      bool skip_mount = false;
      for (const auto w_mount : whitelisted_mounts) {
        if (w_mount.find(mount.mountpoint) == 0 ||
            mount.mountpoint.find(w_mount) == 0) {
          skip_mount = true;
          break;
        }
      }
      if (skip_mount) {
        continue;
      }
    }
    // When we are not using "/" as our root, we skip:
    //  - everything mounted under rootfs_dir AND
    //  - all the mounts along the rootfs_dir
    // For ex., if rootfs_dir is /export/tmpfs/root/, then we will skip the
    // mounts at /export/tmpfs/, /export/tmpfs/root/ and
    // /export/tmpfs/root/bin/.
    // If rootfs_dir == "/", then we unmount everything except "/".
    if ((rootfs_dir != kFsRoot) &&
        ((mount.mountpoint.find(rootfs_dir) == 0) ||
         (rootfs_dir.find(mount.mountpoint) == 0))) {
      continue;
    }

    mountpoints.emplace_back(mount.mountpoint);
  }

  while (!mountpoints.empty()) {
    const string &mountpoint = mountpoints.back();
    if ((GlobalLibcFsApi()->UMount(mountpoint.c_str()) < 0) &&
        (errno != EINVAL)) {
      return Status(INTERNAL,
                    Substitute("umount($0) failed: $1", mountpoint,
                               StrError(errno)));
    }
    mountpoints.pop_back();
  }

  return Status::OK;
}

struct ScopedTmpdirRemover : public ScopedCleanup {
  explicit ScopedTmpdirRemover(const string &dirpath)
      : ScopedCleanup(&ScopedTmpdirRemover::RemoveDir, dirpath) {}

  static void RemoveDir(const string &dirpath) {
    GlobalLibcFsApi()->UMount2(dirpath.c_str(), MNT_DETACH);
    GlobalLibcFsApi()->RmDir(dirpath.c_str());
  }
};

StatusOr<set<string>> FilesystemConfigurator::SetupExternalMounts(
    const ::containers::Mounts &mounts,
    const string &rootfs_path) const {
  set<string> mountpoints;
  for (const auto mount : mounts.mount()) {
    // Return error if both source and target do not exist.
    // Once we start creating targets, we could assume that the absence of
    // target indicates that the mountpoint must be <rootfs_path>/<source path>.
    if (!mount.has_source() || mount.source().empty() ||
        !mount.has_target() || mount.target().empty()) {
      return Status(
          ::util::error::INVALID_ARGUMENT,
          "FilesystemSpec mounts must contain both source and target");
    }
    if (!RETURN_IF_ERROR(GlobalFsUtils()->FileExists(mount.source()))) {
      return Status(
          INTERNAL,
          Substitute("Mount source $0 does not exist.", mount.source()));
    }
    const string mountpoint = ::file::JoinPath(rootfs_path, mount.target());
    if (!RETURN_IF_ERROR(GlobalFsUtils()->FileExists(mountpoint))) {
      return Status(
          INTERNAL,
          Substitute("Mountpoint $0 does not exist.", mountpoint));
    }
    // Re-evaluate recursive mounting by default if it breaks any users.
    set<MountUtils::BindMountOpts> opts({MountUtils::RECURSIVE});
    if (mount.has_read_only() && mount.read_only()) {
      opts.insert(MountUtils::READONLY);
    }
    if (mount.has_private_() && mount.private_()) {
      opts.insert(MountUtils::PRIVATE);
    }
    RETURN_IF_ERROR(GlobalMountUtils()->BindMount(mount.source(), mountpoint,
                                                  opts));
    mountpoints.insert(mountpoint);
  }
  return mountpoints;
}

Status FilesystemConfigurator::SetupPivotRoot(const string &rootfs_path) const {
  // Always chdir to rootfs_path. PivotRoot() doesn't guaranty to change calling
  // process' working directory.
  if (GlobalLibcFsApi()->ChDir(rootfs_path.c_str()) < 0) {
    return Status(INTERNAL,
                  Substitute("chdir($0) failed: $1", rootfs_path,
                             StrError(errno)));
  }

  if (rootfs_path == kFsRoot) {
    // TODO(adityakali): May be we should maintain a minimum skeleton filesystem
    // and bind mount it at a unique dir created for this container. We can then
    // make it as our new rootfs by pivot_root-ing there.
    //
    // For now, nothing to do if we are using the default rootfs.
    return Status::OK;
  }

  // Create a temporary old-root under rootfs_path which will be used to store
  // the old rootfs path.
  const string old_root =
      Substitute("nscon.old_root.$0",
                 GlobalTimeUtils()->MicrosecondsSinceEpoch().value());

  if (GlobalLibcFsApi()->MkDir(old_root.c_str(), 0700) < 0) {
    return Status(INTERNAL,
                  Substitute("mkdir($0): $1", old_root, StrError(errno)));
  }

  ScopedTmpdirRemover tmpdir_remover(old_root);

  if (GlobalLibcFsApi()->PivotRoot(".", old_root.c_str()) < 0) {
    return Status(INTERNAL,
                  Substitute("pivot_root($0, $1): $2", rootfs_path,
                             old_root, StrError(errno)));
  }

  if (GlobalLibcFsApi()->ChDir("/") < 0) {
    return Status(INTERNAL,
                  Substitute("chdir(\"/\") failed: $0", StrError(errno)));
  }

  if (GlobalLibcFsApi()->UMount2(old_root.c_str(), MNT_DETACH) < 0) {
    return Status(INTERNAL,
                  Substitute("umount2($0) failed: $1", old_root,
                             StrError(errno)));
  }

  tmpdir_remover.Cancel();

  if (GlobalLibcFsApi()->RmDir(old_root.c_str()) < 0) {
    return Status(INTERNAL,
                  Substitute("rmdir($0) failed: $1", old_root,
                             StrError(errno)));
  }

  return Status::OK;
}

Status FilesystemConfigurator::SetupChroot(const string &rootfs_path) const {
  // Always chdir to rootfs_path. ChRoot() doesn't guaranty to change calling
  // process' working directory.
  if (GlobalLibcFsApi()->ChDir(rootfs_path.c_str()) < 0) {
    return Status(INTERNAL,
                  Substitute("chdir($0) failed: $1", rootfs_path,
                             StrError(errno)));
  }

  if (rootfs_path == kFsRoot) {
    // Nothing to do if we are using the default rootfs.
    return Status::OK;
  }

  // Move to the new rootfs.
  if (GlobalLibcFsApi()->ChRoot(rootfs_path.c_str()) < 0) {
    return Status(INTERNAL,
                  Substitute("chroot($0): $1", rootfs_path, StrError(errno)));
  }

  return Status::OK;
}

Status FilesystemConfigurator::SetupProcfs(const string &procfs_path) const {
  if (GlobalLibcFsApi()->Mount("proc", procfs_path.c_str(), "proc",
                               kDefaultMountFlags, nullptr) < 0) {
    return Status(INTERNAL,
                  Substitute("procfs mount($0) failed: $1", procfs_path.c_str(),
                             StrError(errno)));
  }
  return Status::OK;
}

Status FilesystemConfigurator::SetupSysfs(const string &sysfs_path) const {
  if (GlobalLibcFsApi()->Mount("sysfs", sysfs_path.c_str(), "sysfs",
                               kDefaultMountFlags, nullptr) < 0) {
    return Status(INTERNAL,
                  Substitute("sysfs mount($0) failed: $1", sysfs_path.c_str(),
                             StrError(errno)));
  }
  return Status::OK;
}

// TODO(vishnuk): Use containers::ConsoleUtil here.
Status FilesystemConfigurator::SetupDevpts() const {
  // We do not want to modify the root file system. So it is expected that
  // /dev and /dev/ptmx directories will exist before invoking nscon.
  if (!RETURN_IF_ERROR(GlobalFsUtils()->FileExists(kDefaultDevptsPath))) {
    return Status(INTERNAL,
                  Substitute("$0 does not exist", kDefaultDevptsPath));
  }

  if (!RETURN_IF_ERROR(GlobalFsUtils()->FileExists(kDevptmxPath))) {
    return Status(INTERNAL, Substitute("$0 does not exist.",
                                                      kDevptmxPath));
  }

  if (GlobalLibcFsApi()->Mount("devpts", kDefaultDevptsPath, "devpts",
                               kDefaultMountFlags, kDevptsMountData) < 0) {
    return Status(INTERNAL,
                  Substitute("devpts mount($0) failed: $1", kDefaultDevptsPath,
                             StrError(errno)));
  }

  // Make /dev/ptmx point to /dev/pts/ptmx. devpts is namespace aware. To
  // provide each namespace with their own set of pty devices, /dev/pts/ptmx
  // must be used to create pty connections instead of /dev/pts. Refer to
  // devpts.txt kernel documentation for more information.
  const string pts_ptmx_path = file::JoinPath(kDefaultDevptsPath, "ptmx");
  bool pts_ptmx_exists =
      RETURN_IF_ERROR(GlobalFsUtils()->FileExists(pts_ptmx_path));

  if (pts_ptmx_exists) {
    // Devpts namespace support exists
    // Make existing /dev/ptmx point to /dev/pts/ptmx using bind mount
    RETURN_IF_ERROR(GlobalMountUtils()->BindMount(pts_ptmx_path, kDevptmxPath,
                                                  {}));
  }

  return Status::OK;
}

// Do atleast the minimum filesystem preparation irrespective of whether
// FilesystemSpec was specified or not. This involves mount namespace cleanup
// and remounting procfs & sysfs. If FilesystemSpec was specified, then use the
// paths from that spec.
Status FilesystemConfigurator::SetupInsideNamespace(
    const NamespaceSpec &spec) const {
  string rootfs_path = kFsRoot;
  bool chroot_to_rootfs = false;
  set<string> whitelisted_mounts;
  if (spec.has_fs()) {
    // Override defaults if specified in fs_spec.
    const FilesystemSpec &fs_spec = spec.fs();
    if (fs_spec.has_rootfs_path()) {
      rootfs_path = file::CleanPath(fs_spec.rootfs_path());
      if (!file::IsAbsolutePath(rootfs_path)) {
        return Status(::util::error::INVALID_ARGUMENT,
                      Substitute("rootfs_path must be absolute: $0",
                                 rootfs_path));
      }
    }

    if (fs_spec.has_chroot_to_rootfs()) {
      chroot_to_rootfs = fs_spec.chroot_to_rootfs();
    }
    whitelisted_mounts = RETURN_IF_ERROR(SetupExternalMounts(
        fs_spec.external_mounts(), rootfs_path));
  }

  RETURN_IF_ERROR(PrepareFilesystem(whitelisted_mounts, rootfs_path));
  if (chroot_to_rootfs) {
    RETURN_IF_ERROR(SetupChroot(rootfs_path));
  } else {
    RETURN_IF_ERROR(SetupPivotRoot(rootfs_path));
  }
  RETURN_IF_ERROR(SetupProcfs(kDefaultProcfsPath));
  RETURN_IF_ERROR(SetupSysfs(kDefaultSysfsPath));

  const bool needs_console = spec.has_run_spec() &&
      spec.run_spec().has_console() &&
      spec.run_spec().console().has_slave_pty();
  Status status = SetupDevpts();
  // Ignore devpts setup error unless user requested console setup.
  if (!status.ok() && needs_console) {
    return status;
  }

  return Status::OK;
}

}  // namespace nscon
}  // namespace containers
