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

#include "lmctfy/controllers/cgroup_controller.h"

#include "file/base/file.h"
#include "file/base/path.h"
#include "file/util/linux_fileops.h"
#include "lmctfy/kernel_files.h"
#include "util/errors.h"
#include "strings/numbers.h"
#include "strings/split.h"
#include "strings/stringpiece.h"
#include "strings/substitute.h"
#include "util/task/codes.pb.h"
#include "util/task/status.h"

using ::file::JoinPath;
using ::util::FileLines;
using ::std::unique_ptr;
using ::std::vector;
using ::strings::SkipEmpty;
using ::strings::Split;
using ::strings::Substitute;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace lmctfy {

CgroupController::CgroupController(CgroupHierarchy type,
                                   const string &cgroup_path, bool owns_cgroup,
                                   const KernelApi *kernel,
                                   EventFdNotifications *eventfd_notifications)
    : type_(type),
      cgroup_path_(cgroup_path),
      owns_cgroup_(owns_cgroup),
      kernel_(CHECK_NOTNULL(kernel)),
      eventfd_notifications_(CHECK_NOTNULL(eventfd_notifications)) {}

CgroupController::~CgroupController() {
}

Status CgroupController::Destroy() {
  // Rmdir the cgroup path if it is owned by this controller.
  if (owns_cgroup_) {
    if (kernel_->RmDir(cgroup_path_) != 0) {
      return Status(::util::error::FAILED_PRECONDITION,
                    Substitute("Failed to rmdir \"$0\"", cgroup_path_));
    }
  }

  delete this;
  return Status::OK;
}

Status CgroupController::Enter(pid_t tid) {
  return SetParamInt(KernelFiles::CGroup::kTasks, tid);
}

StatusOr<vector<pid_t>> CgroupController::GetThreads() const {
  return GetPids(KernelFiles::CGroup::kTasks);
}

StatusOr<vector<pid_t>> CgroupController::GetProcesses() const {
  return GetPids(KernelFiles::CGroup::kProcesses);
}

StatusOr<vector<string>> CgroupController::GetSubcontainers() const {
  return GetSubdirectories();
}

Status CgroupController::EnableCloneChildren() {
  return SetParamBool(KernelFiles::CGroup::Children::kClone, true);
}

Status CgroupController::DisableCloneChildren() {
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
  if (!owns_cgroup_) {
    return Status::OK;
  }
  return SetParamInt(KernelFiles::CGroup::Children::kLimit, value);
}

StatusOr<bool> CgroupController::GetParamBool(
    const string &cgroup_file) const {
  int64 value;
  RETURN_IF_ERROR(GetParamInt(cgroup_file), &value);

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
  string all_pids;
  RETURN_IF_ERROR(GetParamString(cgroup_file), &all_pids);
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

StatusOr<vector<string>> CgroupController::GetSubdirectories() const {
  // Get the subdirectories.
  vector<string> subdirs;
  string error;
  if (!::file_util::LinuxFileOps::ListDirectorySubdirs(cgroup_path_, &subdirs,
                                                       false, &error)) {
    return Status(
        ::util::error::FAILED_PRECONDITION,
        Substitute("Failed to get subdirectories of \"$0\" with error \"$1\"",
                   cgroup_path_, error));
  }

  return subdirs;
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
  kernel_->SafeWriteResFileWithRetry(3, value, file_path, &open_error,
                                     &write_error);

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

}  // namespace lmctfy
}  // namespace containers
