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

#include "lmctfy/file_lock_handler.h"

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include "base/logging.h"
#include "file/base/path.h"
#include "util/errors.h"
#include "strings/stringpiece.h"
#include "strings/substitute.h"
#include "util/task/codes.pb.h"

namespace containers {
namespace lmctfy {
class InitSpec;
}  // namespace lmctfy
}  // namespace containers

using ::file::JoinPath;
using ::util::ScopedCleanup;
using ::strings::Substitute;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace lmctfy {

FileLockHandlerFactory::FileLockHandlerFactory(const string &locks_dir,
                                               const KernelApi *kernel)
    : locks_dir_(locks_dir), kernel_(kernel) {}

string FileLockHandlerFactory::GetLockFilePath(
    const string &container_name) const {
  return JoinPath(locks_dir_, container_name + ".lock");
}

string FileLockHandlerFactory::GetLockDirPath(
    const string &container_name) const {
  return JoinPath(locks_dir_, container_name);
}

StatusOr<LockHandler *> FileLockHandlerFactory::Create(
    const string &container_name) const {
  const string lock_file_path = GetLockFilePath(container_name);
  const string lock_dir_path = GetLockDirPath(container_name);

  // Create file exclusively.
  int lock_fd;
  RETURN_IF_ERROR(CreateLockFile(lock_file_path), &lock_fd);

  ScopedCleanup<Close> closer(lock_fd);
  ScopedCleanup<Unlink> unlinker(lock_file_path);

  // Create lock directory.
  if (mkdir(lock_dir_path.c_str(), 0755) != 0) {
    // Cleanup lock_fd
    return Status(::util::error::FAILED_PRECONDITION,
                  Substitute("Failed to create lock directory \"$0\". It may "
                             "already exist.",
                             lock_dir_path));
  }

  return new FileLockHandler(closer.release(), unlinker.release(),
                             lock_dir_path, (container_name == "/"));
}

StatusOr<LockHandler *> FileLockHandlerFactory::Get(
    const string &container_name) const {
  const string lock_file_path = GetLockFilePath(container_name);

  // Open the lockfile if it exists.
  int lock_fd = open(lock_file_path.c_str(), O_RDONLY | O_CLOEXEC);
  if (lock_fd < 0) {
    return Status(::util::error::NOT_FOUND,
                  Substitute("Failed to open lockfile \"$0\". It may not exist",
                             lock_file_path));
  }

  return new FileLockHandler(lock_fd, lock_file_path,
                             GetLockDirPath(container_name),
                             (container_name == "/"));
}

Status FileLockHandlerFactory::InitMachine(const InitSpec &spec) const {
  // Ensure locks_dir_ exists.
  if (kernel_->MkDirRecursive(locks_dir_) != 0) {
    return Status(
        ::util::error::FAILED_PRECONDITION,
        Substitute("Failed to recursively create lock directory \"$0\"",
                   locks_dir_));
  }

  // Ensure the root lockfile exists;
  const string root_lock_file = GetLockFilePath("/");
  if (!kernel_->FileExists(root_lock_file)) {
    int ignored;
    RETURN_IF_ERROR(CreateLockFile(root_lock_file), &ignored);
  }

  return Status::OK;
}

StatusOr<int> FileLockHandlerFactory::CreateLockFile(
    const string &lock_file_path) const {
  // Create file exclusive.
  int lock_fd = open(lock_file_path.c_str(),
                     O_RDONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0664);
  if (lock_fd < 0) {
    return Status(::util::error::FAILED_PRECONDITION,
                  Substitute("Failed to create lockfile \"$0\". It may already "
                             "exist.",
                             lock_file_path));
  }

  return lock_fd;
}

FileLockHandler::FileLockHandler(int lock_fd,
                                 const string &lock_file_path,
                                 const string &lock_dir_path,
                                 bool is_root)
    : lock_fd_(lock_fd),
      lock_file_path_(lock_file_path),
      lock_dir_path_(lock_dir_path),
      is_root_(is_root) ,
      current_lock_state_(STATE_UNLOCKED) {}

FileLockHandler::~FileLockHandler() {}

Status FileLockHandler::Destroy() {
  // Disallow the destruction of the root container's lock.
  if (is_root_) {
    return Status(::util::error::PERMISSION_DENIED,
                  "Cannot destroy LockHandler of the root container.");
  }

  {
    WriterMutexLock l(&intraprocess_lock_);

    // Grab an exclusive lock.
    if (flock(lock_fd_.get(), LOCK_EX) != 0) {
      return Status(::util::error::UNAVAILABLE,
                    Substitute("Failed to lock lockfile \"$0\" for "
                               "destruction.",
                               lock_file_path_));
    }

    // Destroy the lock directory. Don't fail if it is already destroyed.
    struct stat unused;
    if (rmdir(lock_dir_path_.c_str()) != 0
        && stat(lock_dir_path_.c_str(), &unused) == 0) {
      return Status(::util::error::FAILED_PRECONDITION,
                    Substitute("Failed to delete lock directory \"$0\" during "
                               " destruction. It may not be empty.",
                               lock_dir_path_));
    }

    // Destroy the lock file. Don't fail if it is already destroyed.
    if (unlink(lock_file_path_.c_str()) != 0
        && stat(lock_file_path_.c_str(), &unused) == 0) {
      return Status(::util::error::UNAVAILABLE,
                    Substitute("Failed to delete lockfile \"$0\" during "
                               "destruction.",
                               lock_file_path_));
    }
  }

  delete this;
  return Status::OK;
}

Status FileLockHandler::ExclusiveLock() {
  intraprocess_lock_.WriterLock();

  // Unlock if there was an error grabbing the lock.
  Status status = GrabFileLock(LOCK_EX);
  if (!status.ok()) {
    intraprocess_lock_.WriterUnlock();
    return status;
  }

  {
    MutexLock l(&state_lock_);
    current_lock_state_ = STATE_EXCLUSIVE;
  }
  return Status::OK;
}

Status FileLockHandler::SharedLock() {
  intraprocess_lock_.ReaderLock();

  // If we already have the lock, don't bother getting it again.
  MutexLock l(&state_lock_);
  if (current_lock_state_ != STATE_SHARED) {
    // Unlock if there was an error grabbing the lock.
    Status status = GrabFileLock(LOCK_SH);
    if (!status.ok()) {
      intraprocess_lock_.ReaderUnlock();
      return status;
    }

    current_lock_state_ = STATE_SHARED;
  }

  return Status::OK;
}

Status FileLockHandler::GrabFileLock(int operation) {
  // Grab the file lock.
  if (flock(lock_fd_.get(), operation) != 0) {
    return Status(::util::error::UNAVAILABLE,
                  Substitute("Failed to lock lockfile \"$0\".",
                             lock_file_path_));
  }

  // Check that the lockfile is still there.
  struct stat unused;
  if (stat(lock_file_path_.c_str(), &unused) != 0) {
    return Status(::util::error::NOT_FOUND,
                  Substitute("The lockfile \"$0\" no longer exists. The "
                             "container may no longer exist.",
                             lock_file_path_));
  }

  return Status::OK;
}

void FileLockHandler::Unlock() {
  if (flock(lock_fd_.get(), LOCK_UN) != 0) {
    LOG(WARNING) << "Failed to unlock lockfile \"" << lock_file_path_ << "\".";
  }

  // Unlock the lock type that is currently being held.
  MutexLock l(&state_lock_);
  if (current_lock_state_ == STATE_EXCLUSIVE) {
    intraprocess_lock_.WriterUnlock();
  } else {
    intraprocess_lock_.ReaderUnlock();
  }
  current_lock_state_ = STATE_UNLOCKED;
}

}  // namespace lmctfy
}  // namespace containers
