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

#include "lmctfy/controllers/cgroup_controller.h"

#include "file/base/file.h"
#include "file/base/path.h"
#include "lmctfy/kernel_files.h"
#include "global_utils/fs_utils.h"
#include "util/errors.h"
#include "util/scoped_cleanup.h"
#include "system_api/libc_fs_api.h"
#include "strings/numbers.h"
#include "strings/split.h"
#include "strings/stringpiece.h"
#include "strings/substitute.h"
#include "util/task/codes.pb.h"
#include "util/task/status.h"

using ::file::Basename;
using ::file::JoinPath;
using ::util::FileLines;
using ::util::GlobalFsUtils;
using ::system_api::GlobalLibcFsApi;
using ::util::UnixGid;
using ::util::UnixUid;
using ::util::ScopedCleanup;
using ::std::unique_ptr;
using ::std::vector;
using ::strings::SkipEmpty;
using ::strings::Split;
using ::strings::Substitute;
using ::util::error::FAILED_PRECONDITION;
using ::util::error::INTERNAL;
using ::util::error::INVALID_ARGUMENT;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace lmctfy {

CgroupController::CgroupController(CgroupHierarchy type,
                                   const string &hierarchy_path,
                                   const string &cgroup_path, bool owns_cgroup,
                                   const KernelApi *kernel,
                                   EventFdNotifications *eventfd_notifications)
    : type_(type),
      hierarchy_path_(hierarchy_path),
      cgroup_path_(cgroup_path),
      owns_cgroup_(owns_cgroup),
      kernel_(CHECK_NOTNULL(kernel)),
      eventfd_notifications_(CHECK_NOTNULL(eventfd_notifications)) {}

CgroupController::~CgroupController() {
}

Status CgroupController::Destroy() {
  // Rmdir the cgroup path if it is owned by this controller.
  if (owns_cgroup_) {
    RETURN_IF_ERROR(DeleteCgroupHierarchy(cgroup_path_));
  }

  delete this;
  return Status::OK;
}

Status CgroupController::Enter(pid_t tid) {
  // No-op if the underlying cgroup is not owned by this controller.
  if (!owns_cgroup_) {
    return Status::OK;
  }

  return SetParamInt(KernelFiles::CGroup::kTasks, tid);
}

Status CgroupController::Delegate(UnixUid uid, UnixGid gid) {
  // No-op if the underlying cgroup is not owned by this controller.
  if (!owns_cgroup_) {
    return Status::OK;
  }

  // Chown the cgroup itself so that the user/group can create subcontainers.
  if (kernel_->Chown(cgroup_path_, uid.value(), gid.value()) != 0) {
    return Status(FAILED_PRECONDITION, Substitute(
        "Failed to chown cgroup path \"$0\" to UID $1 and GID $2 "
        "with error: $3", cgroup_path_, uid.value(), gid.value(), errno));
  }

  // Chown the tasks file so that the user/group can enter the container.
  const string kTasksFile = CgroupFilePath(KernelFiles::CGroup::kTasks);
  if (kernel_->Chown(kTasksFile, uid.value(), gid.value()) != 0) {
    return Status(FAILED_PRECONDITION, Substitute(
        "Failed to chown tasks file \"$0\" to UID $1 and GID $2 "
        "with error: $3", kTasksFile, uid.value(), gid.value(), errno));
  }

  return Status::OK;
}

StatusOr<vector<pid_t>> CgroupController::GetThreads() const {
  return GetPids(KernelFiles::CGroup::kTasks);
}

StatusOr<vector<pid_t>> CgroupController::GetProcesses() const {
  return GetPids(KernelFiles::CGroup::kProcesses);
}

StatusOr<vector<string>> CgroupController::GetSubcontainers() const {
  vector<string> subdirs;
  RETURN_IF_ERROR(GetSubdirectories(cgroup_path_, &subdirs));
  for (string &path : subdirs) {
    path = Basename(path).ToString();
  }
  return subdirs;
}
Status CgroupController::EnableCloneChildren() {
  // No-op if the underlying cgroup is not owned by this controller.
  if (!owns_cgroup_) {
    return Status::OK;
  }

  return SetParamBool(KernelFiles::CGroup::Children::kClone, true);
}

Status CgroupController::DisableCloneChildren() {
  // No-op if the underlying cgroup is not owned by this controller.
  if (!owns_cgroup_) {
    return Status::OK;
  }

  return SetParamBool(KernelFiles::CGroup::Children::kClone, false);
}

Status CgroupController::SetParamBool(const string &cgroup_file,
                                      bool value) {
  return SetParamString(cgroup_file, value ? "1" : "0");
}

Status CgroupController::SetParamInt(const string &cgroup_file,
                                     int64 value) {
  return SetParamString(cgroup_file, Substitute("$0", value));
}

Status CgroupController::SetParamString(const string &cgroup_file,
                                        const string &value) {
  const string file_path = CgroupFilePath(cgroup_file);
  return WriteStringToFile(file_path, value);
}

Status CgroupController::SetChildrenLimit(int64 value) {
  // No-op if the underlying cgroup is not owned by this controller.
  if (!owns_cgroup_) {
    return Status::OK;
  }

  return SetParamInt(KernelFiles::CGroup::Children::kLimit, value);
}

StatusOr<bool> CgroupController::GetParamBool(
    const string &cgroup_file) const {
  int64 value = RETURN_IF_ERROR(GetParamInt(cgroup_file));

  switch (value) {
    case 0:
      return false;
    case 1:
      return true;
    default:
      return Status(::util::error::OUT_OF_RANGE,
                    Substitute("Value \"$0\" out of range for a bool", value));
  }
}

StatusOr<int64> CgroupController::GetParamInt(
    const string &cgroup_file) const {
  StatusOr<string> statusor = GetParamString(cgroup_file);
  if (!statusor.ok()) {
    return statusor.status();
  }

  int64 output;
  if (!SimpleAtoi(statusor.ValueOrDie().c_str(), &output)) {
    return Status(::util::error::FAILED_PRECONDITION,
                  Substitute("Failed to parse int from string \"$0\"",
                             statusor.ValueOrDie()));
  }

  return output;
}

StatusOr<string> CgroupController::GetParamString(
    const string &cgroup_file) const {
  return ReadStringFromFile(CgroupFilePath(cgroup_file));
}

// TODO(vmarmol): Implement this iteratively.
StatusOr<vector<pid_t>> CgroupController::GetPids(
    const string &cgroup_file) const {
  string all_pids = RETURN_IF_ERROR(GetParamString(cgroup_file));
  const vector<string> pid_strings = Split(all_pids, "\n", SkipEmpty());

  // Parse all the PIDs.
  vector<pid_t> pids;
  pids.reserve(pid_strings.size());
  for (const string &pid_string : pid_strings) {
    pid_t pid;
    if (!SimpleAtoi(pid_string, &pid)) {
      return Status(::util::error::FAILED_PRECONDITION,
                    Substitute("Unknown PID \"$0\" found in cgroup file \"$1\"",
                               pid_string, cgroup_file));
    }

    pids.push_back(pid);
  }

  return pids;
}

StatusOr<FileLines> CgroupController::GetParamLines(
    const string &cgroup_file) const {
  const string file_path = CgroupFilePath(cgroup_file);

  // Ensure the file exists.
  if (kernel_->Access(file_path, F_OK) != 0) {
    return Status(::util::error::NOT_FOUND,
                  Substitute("File \"$0\" not found", file_path));
  }

  return FileLines(file_path);
}

// Note: GetSubdirectories must insert the subdirectories to the back
// of the provided vector.
Status CgroupController::GetSubdirectories(const string &path,
                                           vector<string> *entries) const {
  DIR *dir = GlobalLibcFsApi()->OpenDir(path.c_str());
  if (dir == nullptr) {
    return Status(FAILED_PRECONDITION, Substitute(
        "Unable to open directory \"$0\" with error \"$1\"",
        path, StrError(errno)));
  }
  ScopedCleanup cleanup([dir]() {
    GlobalLibcFsApi()->CloseDir(dir);
  });
  struct dirent readdir_buf, *de = nullptr;
  while (GlobalLibcFsApi()->ReadDirR(dir, &readdir_buf, &de) == 0 &&
         de != nullptr) {
    if (strcmp(de->d_name, ".") != 0 && strcmp(de->d_name, "..") != 0) {
      string full_path = file::JoinPath(path, de->d_name);
      Status status = GlobalFsUtils()->DirExists(full_path);
      if (status.ok()) {
        entries->emplace_back(full_path);
      } else if (status.error_code() != INVALID_ARGUMENT) {
        return status;
      }
    }
  }
  return Status::OK;
}

Status CgroupController::DeleteCgroupHierarchy(const string &path) const {
  vector<string> dirs_to_delete;
  vector<string> dirs_to_check;

  dirs_to_check.push_back(path);
  while (!dirs_to_check.empty()) {
    // Get the last element from dirs_to_check
    const string current_path = dirs_to_check.back();
    dirs_to_check.pop_back();

    RETURN_IF_ERROR(GetSubdirectories(current_path, &dirs_to_check));

    dirs_to_delete.emplace_back(current_path);
  }

  while (!dirs_to_delete.empty()) {
    const string current_path = dirs_to_delete.back();
    if (kernel_->RmDir(current_path) < 0) {
      return Status(FAILED_PRECONDITION, Substitute(
          "Unable to delete directory \"$0\" with error \"$1\"",
          current_path, StrError(errno)));
    }
    dirs_to_delete.pop_back();
  }
  return Status::OK;
}

StatusOr<int64> CgroupController::GetChildrenLimit() const {
  return GetParamInt(KernelFiles::CGroup::Children::kLimit);
}

StatusOr<ActiveNotifications::Handle> CgroupController::RegisterNotification(
    const string &cgroup_file, const string &arguments,
    EventCallback *callback) {
  CHECK_NOTNULL(callback);
  callback->CheckIsRepeatable();

  return eventfd_notifications_->RegisterNotification(cgroup_path_, cgroup_file,
                                                      arguments, callback);
}

// TODO(vmarmol): We currently do feature detection on each read/write, this is
// very time consuming and prone to flakyness. We should move towards feature
// detection in init (existing bug).
// TODO(vmarmol): Use GetFileContents().
StatusOr<string> CgroupController::ReadStringFromFile(
    const string &file_path) const {
  // Ensure the file exists.
  if (kernel_->Access(file_path, F_OK) != 0) {
    return Status(::util::error::NOT_FOUND,
                  Substitute("File \"$0\" not found", file_path));
  }

  // Read the file.
  string output;
  if (!kernel_->ReadFileToString(file_path, &output)) {
    return Status(
        ::util::error::FAILED_PRECONDITION,
        Substitute("Failed to read contents of file \"$0\"", file_path));
  }

  return output;
}

Status CgroupController::WriteStringToFile(const string &file_path,
                                           const string &value) const {
  bool open_error = false;
  bool write_error = false;
  kernel_->SafeWriteResFile(value, file_path, &open_error, &write_error);

  // Check errors.
  if (open_error) {
    return Status(::util::error::NOT_FOUND,
                  Substitute("Failed to open file \"$0\" for hierarchy $1",
                             file_path, type_));
  }
  if (write_error) {
    return Status(
        ::util::error::UNAVAILABLE,
        Substitute("Failed to write \"$0\" to file \"$1\" for hierarchy $2",
                   value, file_path, type_));
  }

  return Status::OK;
}

string CgroupController::CgroupFilePath(const string &cgroup_file) const {
  return JoinPath(cgroup_path_, cgroup_file);
}

Status CgroupController::PopulateMachineSpec(MachineSpec *spec) const {
  auto *virt_root = spec->mutable_virtual_root()->add_cgroup_virtual_root();
  virt_root->set_root(hierarchy_path_);
  virt_root->set_hierarchy(type_);
  return Status::OK;
}

}  // namespace lmctfy
}  // namespace containers
