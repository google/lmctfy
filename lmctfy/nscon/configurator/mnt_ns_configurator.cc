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
// MntNsConfigurator implementation.
//
#include "nscon/configurator/mnt_ns_configurator.h"

#include <sys/mount.h>

#include "file/base/path.h"
#include "include/namespaces.pb.h"
#include "global_utils/mount_utils.h"
#include "util/errors.h"
#include "system_api/libc_fs_api.h"
#include "strings/stringpiece.h"
#include "strings/substitute.h"
#include "util/task/status.h"

using ::system_api::GlobalLibcFsApi;
using ::util::GlobalMountUtils;
using ::strings::Substitute;
using ::util::error::INTERNAL;
using ::util::error::INVALID_ARGUMENT;
using ::util::Status;

namespace containers {
namespace nscon {

Status MntNsConfigurator::DoUnmountAction(const Unmount &unmount_action) const {
  if (!unmount_action.has_path()) {
    // Nothing to do.
    return Status::OK;
  }

  const string path(unmount_action.path());
  if (path.empty()) {
    return Status(INVALID_ARGUMENT, "Unmount path cannot be empty");
  }

  if (!file::IsAbsolutePath(path)) {
    return Status(INVALID_ARGUMENT,
                  Substitute("Must specify absolute path: $0", path));
  }

  if (unmount_action.has_do_recursive() && unmount_action.do_recursive()) {
    return GlobalMountUtils()->UnmountRecursive(path);
  }

  // We successfully unmounted this directory or it doesn't exist.
  if ((GlobalLibcFsApi()->UMount(path.c_str()) == 0) ||
      (errno == ENOENT) || (errno == EINVAL)) {
    return Status::OK;
  }

  return Status(::util::error::INTERNAL,
                Substitute("umount($0) failed: $1", path, strerror(errno)));
}

Status MntNsConfigurator::DoMountAction(const Mount &mount_action) const {
  if (!mount_action.has_source() || !mount_action.has_target() ||
      mount_action.source().empty() || mount_action.target().empty()) {
    return Status(INVALID_ARGUMENT, "Must specify both source and target");
  }

  if (!file::IsAbsolutePath(mount_action.target())) {
    return Status(INVALID_ARGUMENT,
                  Substitute("Mount target must be absolute path: $0",
                             mount_action.target()));
  }

  string fstype = mount_action.has_fstype() ? mount_action.fstype() : "";
  uint64 flags = mount_action.has_flags() ? mount_action.flags() : 0;
  string options = mount_action.has_options() ? mount_action.options() : "";

  if (GlobalLibcFsApi()->Mount(mount_action.source().c_str(),
                               mount_action.target().c_str(), fstype.c_str(),
                               flags, options.c_str()) != 0) {
    return Status(INTERNAL,
                  Substitute("mount(source=$0, target=$1) failed: $2",
                             mount_action.source(), mount_action.target(),
                             strerror(errno)));
  }

  return Status::OK;
}

Status
MntNsConfigurator::SetupInsideNamespace(const NamespaceSpec &spec) const {
  if (!spec.has_mnt()) {
    return Status::OK;
  }

  const MntNsSpec &mnt_spec = spec.mnt();
  for (auto action : mnt_spec.mount_action()) {
    if (action.has_unmount() && action.has_mount()) {
      // Only one can be specified.
      return Status(INVALID_ARGUMENT, "Only one of Mount or Unmount can be "
                                      "specified per MountAction");
    }

    if (action.has_unmount()) {
      RETURN_IF_ERROR(DoUnmountAction(action.unmount()));
    } else if (action.has_mount()) {
      RETURN_IF_ERROR(DoMountAction(action.mount()));
    }
  }

  return Status::OK;
}

}  // namespace nscon
}  // namespace containers
