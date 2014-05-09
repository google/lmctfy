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
// MachineConfigurator implementation.
//
#include "nscon/configurator/machine_configurator.h"

#include "file/base/file.h"
#include "file/base/helpers.h"
#include "file/base/path.h"
#include "google/protobuf/text_format.h"
#include "lmctfy/util/global.h"
#include "include/namespaces.pb.h"
#include "global_utils/fs_utils.h"
#include "global_utils/mount_utils.h"
#include "util/safe_types/bytes.h"
#include "util/errors.h"
#include "util/task/status.h"

using ::file::JoinPath;
using ::util::Bytes;
using ::util::GlobalFsUtils;
using ::util::GlobalMountUtils;
using ::util::error::INTERNAL;
using ::util::Status;

namespace containers {
namespace nscon {

// Helper method to create /run as a tmpfs if it does not already exist.
// NOTE: This will modify the filesystem by creating '/run' directory first
// if it doesn't exist.
Status MachineConfigurator::SetupRunTmpfs() const {
  // Need to verify that there is a tmpfs mount over kRunPath,
  // if not need to mount over it. That is all.
  Status dir_status = GlobalMountUtils()->GetMountInfo(kRunPath).status();
  if (dir_status.CanonicalCode() == ::util::error::NOT_FOUND) {
    RETURN_IF_ERROR(GlobalFsUtils()->SafeEnsureDir(kRunPath, kRunMode));
    return GlobalMountUtils()->MountTmpfs(kRunPath,
                                          Bytes(kRunTmpfsDefaultSize), {});
  }
  return dir_status;
}

Status MachineConfigurator::WriteMachineSpec(const MachineSpec &spec,
                                             const string &directory,
                                             const string &filename) const {
  RETURN_IF_ERROR(GlobalFsUtils()->SafeEnsureDir(directory, kRunMode));
  string output;
  google::protobuf::TextFormat::PrintToString(spec, &output);
  return file::SetContents(JoinPath(directory, filename),
                           output, file::Defaults());
}

Status MachineConfigurator::SetupInsideNamespace(
    const NamespaceSpec &spec) const {
  if (!spec.has_fs() || !spec.fs().has_machine()) {
    return Status::OK;
  }

  // Failures here are ignored since writing the machine spec is best effort.
  if (SetupRunTmpfs().ok()) {
    WriteMachineSpec(spec.fs().machine(),
                     JoinPath(kRunPath, kMachineSpecSubDir),
                     kMachineSpecFilename).IgnoreError();
  }

  return Status::OK;
}

}  // namespace nscon
}  // namespace containers
