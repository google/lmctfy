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
// UserNsConfigurator implementation.
//
#include "nscon/configurator/user_ns_configurator.h"

#include <fcntl.h>
#include <vector>

#include "file/base/path.h"
#include "include/namespaces.pb.h"
#include "util/errors.h"
#include "system_api/libc_fs_api.h"
#include "strings/substitute.h"
#include "util/task/status.h"
#include "util/task/statusor.h"

using ::system_api::GlobalLibcFsApi;
using ::system_api::ScopedFileCloser;
using ::std::vector;
using ::strings::Substitute;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace nscon {

Status
UserNsConfigurator::WriteIdMap(const string &id_map_file,
                               const vector<IdMapEntry> &id_map) const {
  if (!id_map.size()) {
    return Status::OK;  // Nothing to do.
  }

  int fd = GlobalLibcFsApi()->Open(id_map_file.c_str(), O_WRONLY);
  if (fd < 0) {
    return Status(::util::error::INTERNAL,
                  Substitute("open($0) failed: $1", id_map_file,
                             strerror(errno)));
  }

  // Auto-close the 'fd' on error.
  ScopedFileCloser fd_closer(fd);

  // We can only do 1 write to the map file. So build the data to write first.
  string map_data;
  for (const IdMapEntry entry : id_map) {
    map_data += Substitute("$0 $1 $2\n", entry.id_inside_ns(),
                           entry.id_outside_ns(), entry.length());
  }

  // Now write the data.
  if (GlobalLibcFsApi()->Write(fd, map_data.c_str(), map_data.length()) < 0) {
    return Status(::util::error::INTERNAL,
                  Substitute("write($0) failed: $1", id_map_file,
                             strerror(errno)));
  }

  fd_closer.Cancel();
  if (GlobalLibcFsApi()->Close(fd) < 0) {
    return Status(::util::error::INTERNAL,
                  Substitute("close($0) failed: $1", id_map_file,
                             strerror(errno)));
  }

  return Status::OK;
}

StatusOr<vector<IdMapEntry>> UserNsConfigurator::ValidateIdMap(
    const ::google::protobuf::RepeatedPtrField<IdMapEntry> &id_map) const {
  vector<IdMapEntry> id_list;
  for (IdMapEntry entry : id_map) {
    if (!entry.has_id_inside_ns() || !entry.has_id_outside_ns() ||
        !entry.has_length()) {
      return Status(::util::error::INVALID_ARGUMENT,
                    "Must specify all fields in IdMapEntry");
    }
    id_list.push_back(entry);
  }
  return id_list;
}

Status UserNsConfigurator::SetupUserNamespace(const UserNsSpec &user_spec,
                                              pid_t init_pid) const {
  if (user_spec.uid_map_size()) {
    const string kUidMapFile = Substitute("/proc/$0/uid_map", init_pid);
    vector<IdMapEntry> id_map =
        RETURN_IF_ERROR(ValidateIdMap(user_spec.uid_map()));
    RETURN_IF_ERROR(WriteIdMap(kUidMapFile, id_map));
  }

  if (user_spec.gid_map_size()) {
    const string kGidMapFile = Substitute("/proc/$0/gid_map", init_pid);
    vector<IdMapEntry> id_map =
        RETURN_IF_ERROR(ValidateIdMap(user_spec.gid_map()));
    RETURN_IF_ERROR(WriteIdMap(kGidMapFile, id_map));
  }

  return Status::OK;
}

Status
UserNsConfigurator::SetupOutsideNamespace(const NamespaceSpec &spec,
                                          pid_t init_pid) const {
  if (!spec.has_user()) {
    return Status::OK;
  }

  return SetupUserNamespace(spec.user(), init_pid);
}

}  // namespace nscon
}  // namespace containers
