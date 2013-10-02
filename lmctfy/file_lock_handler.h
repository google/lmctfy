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

#ifndef SRC_FILE_LOCK_HANDLER_H_
#define SRC_FILE_LOCK_HANDLER_H_

#include <unistd.h>
#include <string>
using ::std::string;

#include "base/macros.h"
#include "base/mutex.h"
#include "base/port.h"
#include "base/thread_annotations.h"
#include "system_api/kernel_api.h"
#include "lmctfy/lock_handler.h"
#include "util/scoped_cleanup.h"
#include "util/task/statusor.h"

namespace containers {
namespace lmctfy {

class InitSpec;

typedef ::system_api::KernelAPI KernelApi;

// Cleanup action to close a file.
class Close {
 public:
  typedef const int ValueType;

  static void Cleanup(const int &fd) {
    close(fd);
  }
};

// Cleanup action to unlink a file.
class Unlink {
 public:
  typedef const string ValueType;

  static void Cleanup(const string &file_path) {
    unlink(file_path.c_str());
  }
};

// LockHandlers implementation based on file locks.
//
// These locks should function like regular mutexes except that they will also
// work accross processes. They are unique to the container name given at
// creation time. This means that any call to Get() (from a different thread or
// a different process) with the same container name, will return a
// LockHandler object that synchronizes all callers of all LockHandler objects
// associated with the specified container (regardless of process or thread).
//
// An important note is that it is likely and acceptable for lock operations to
// fail with LockHandlers.
//
// In order to support locks outside and inside the process, we use both file
// locks and regular reader-writer locks. The reader-writer lock must always be
// taken before the file locks are taken. File locks are built as a hierarchy of
// file locks and file directories:
//
//  NOTE: Assume locks base directory is /locks
//
// | Container Name | File Lock Path          | File Directory Path |
// | /              | /locks/.lock            | /locks/             |
// | /sys           | /locks/sys.lock         | /locks/sys/         |
// | /sys/subcont   | /locks/sys/subcont.lock | /locks/sys/subcont/ |
//
// Class is thread-safe.
class FileLockHandlerFactory : public LockHandlerFactory {
 public:
  // TODO(vmarmol): Switch class to using KernelApi instead of calling raw sys
  // functions.
  // The directory where the lock hierarchy will be stored. Does not own kernel.
  explicit FileLockHandlerFactory(const string &locks_dir,
                                  const KernelApi *kernel_);

  // TODO(vmarmol): Move all container names to a Path type.
  virtual ::util::StatusOr<LockHandler *> Create(
      const string &container_name) const;
  virtual ::util::StatusOr<LockHandler *> Get(
      const string &container_name) const;
  virtual ::util::Status InitMachine(const InitSpec &spec) const;

 private:
  // Gets the location of the lockfile for the specified container.
  // Lock files are at: locks_dir_ + "/" + container_name + ".lock"
  string GetLockFilePath(const string &container_name) const;

  // Gets the location of the lock directory for the specified container.
  // Lock directories are at: locks_dir_ + "/" + container_name
  string GetLockDirPath(const string &container_name) const;

  // Exclusively creates the specified lockfile and return the FD on success.
  ::util::StatusOr<int> CreateLockFile(const string &lock_file_path) const;

  // The directory where lock hierarchy will be stored.
  const string locks_dir_;

  // Wrapper for all calls to the kernel.
  const KernelApi *kernel_;

  DISALLOW_COPY_AND_ASSIGN(FileLockHandlerFactory);
};

// Class is thread-safe.
class LOCKABLE FileLockHandler : public LockHandler {
 public:
  FileLockHandler(int lock_fd,
                  const string &lock_file_path,
                  const string &lock_dir_path,
                  bool is_root);
  virtual ~FileLockHandler();

  virtual ::util::Status Destroy();
  virtual ::util::Status ExclusiveLock() MUST_USE_RESULT
      EXCLUSIVE_LOCK_FUNCTION();
  virtual ::util::Status SharedLock() MUST_USE_RESULT SHARED_LOCK_FUNCTION();
  virtual void Unlock() UNLOCK_FUNCTION();

 private:
  // Current state of this lock.
  enum LockState {
    // An exclusive lock is currently held.
    STATE_EXCLUSIVE,

    // A shared lock is currently held.
    STATE_SHARED,

    // No lock is currently held.
    STATE_UNLOCKED
  };

  // Grabs a file lock of the type specified by operation. Operation must be one
  // of LOCK_EX or LOCK_SH.
  ::util::Status GrabFileLock(int operation);

  // The file descriptor of the open lockfile.
  const ::util::ScopedCleanup<Close> lock_fd_;

  // Path to the lockfile. This is the file we grab file locks on.
  const string lock_file_path_;

  // Path to the lock directory. This is the directory where subcontainers will
  // place their locks.
  const string lock_dir_path_;

  // Whether the lock is for the root container. The root container is treated
  // differently as it cannot be destroyed.
  const bool is_root_;

  // The current state of the lockfile (what lock this LockHandler holds). It is
  // NOT different for each thread that interacts with this LockHandler.
  LockState current_lock_state_ GUARDED_BY(state_lock_);

  // Lock for mutable internal state.
  Mutex state_lock_;

  // Lock for internal operations (internal to threads in this process this
  // LockHandler).
  Mutex intraprocess_lock_;

  DISALLOW_COPY_AND_ASSIGN(FileLockHandler);
};

}  // namespace lmctfy
}  // namespace containers

#endif  // SRC_FILE_LOCK_HANDLER_H_
