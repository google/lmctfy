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

#ifndef SRC_LOCK_HANDLER_H_
#define SRC_LOCK_HANDLER_H_

#include <string>
using ::std::string;

#include "base/macros.h"
#include "include/lmctfy.pb.h"
#include "util/task/statusor.h"
#include "util/task/status.h"

namespace containers {
namespace lmctfy {

class LockHandler;

// LockHandlerFactory generates LockHandlers which provide the locking mechanism
// for a container. Each container will have its own LockHandler to handle
// exclusive and shared locking for container operations.
//
// Factory for creating LockHandlers.
class LockHandlerFactory {
 public:
  virtual ~LockHandlerFactory() {}

  // Create a LockHandler for the specified container. Fails if the container
  // already has an existing lock.
  virtual ::util::StatusOr<LockHandler *> Create(
      const string &container_name) const = 0;

  // Gets a LockHandler for the existing container. Fails if the container does
  // not have an existing lock.
  virtual ::util::StatusOr<LockHandler *> Get(
      const string &container_name) const = 0;

  // Initialize the lock handler on this machine. This setup is idempotent and
  // only needs to be done once at machine bootup.
  virtual ::util::Status InitMachine(const InitSpec &spec) const = 0;

 protected:
  LockHandlerFactory() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(LockHandlerFactory);
};

class LOCKABLE LockHandler {
 public:
  virtual ~LockHandler() {}

  // Destroys the underlying lock. This should only be called when a container
  // is being destroyed. An exclusive lock is acquired before destruction so
  // no lock should be held before Destroy() is called. On success, the
  // LockHandler object is also deleted.
  // NOTE: The root container's lock CANNOT be destroyed. Destroy() will always
  // fail on that lock.
  virtual ::util::Status Destroy() LOCKS_EXCLUDED(this) = 0;

  // Grab an exclusive lock. Only one thread may hold this type of lock at a
  // time. Returns OK on success and iff the lock is acquired.
  virtual ::util::Status ExclusiveLock() MUST_USE_RESULT
      EXCLUSIVE_LOCK_FUNCTION() = 0;

  // Grab a shared lock. Any number of threads may hold this type of lock at a
  // time (but no exclusive locks). Returns OK on success and iff the lock is
  // acquired.
  virtual ::util::Status SharedLock() MUST_USE_RESULT
      SHARED_LOCK_FUNCTION() = 0;

  // Release the lock.
  virtual void Unlock() UNLOCK_FUNCTION() = 0;

 protected:
  LockHandler() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(LockHandler);
};

// These are scoped locking functions provided for completeness. Users should
// use ScopedExclusiveLock or ScopedSharedLock over ScopedBaseLock.
//
// These try to grab the specified lock on construction and releases it on
// destruction. Users MUST check the status of the underlying lock to ensure the
// lock was actually acquire. It is NOT uncommon for lock operations to fail in
// LockHandlers.
//
// e.g.:
//
//   ScopedExclusiveLock l(&lock_handler);
//   if (!l.held()) {
//     return ERROR;
//   }
//
//   // Critical section
//
//   return OK;
//
class ScopedBaseLock {
 public:
  // Returns whether the underlying lock is held.
  bool held() {
    return lock_status_.ok();
  }

  // The Status returned by the call to lock.
  ::util::Status lock_status() {
    return lock_status_;
  }

 protected:
  // The type of lock to acquire.
  enum LockType {
    SCOPED_EXCLUSIVE_LOCK,
    SCOPED_SHARED_LOCK
  };

  ScopedBaseLock(LockHandler *lock, LockType type)
      : lock_(lock) {
    if (type == SCOPED_EXCLUSIVE_LOCK) {
      lock_status_ = lock_->ExclusiveLock();
    } else {
      lock_status_ = lock_->SharedLock();
    }
  }

  ~ScopedBaseLock() {
    // Unlock iff the lock was acquired.
    if (lock_status_.ok()) {
      lock_->Unlock();
    }
  }

 private:
  LockHandler *lock_;
  ::util::Status lock_status_;

  DISALLOW_COPY_AND_ASSIGN(ScopedBaseLock);
};

class SCOPED_LOCKABLE ScopedExclusiveLock : public ScopedBaseLock {
 public:
  explicit ScopedExclusiveLock(LockHandler *lock) EXCLUSIVE_LOCK_FUNCTION(lock)
      : ScopedBaseLock(lock, SCOPED_EXCLUSIVE_LOCK) {}

  ~ScopedExclusiveLock() UNLOCK_FUNCTION() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(ScopedExclusiveLock);
};

class SCOPED_LOCKABLE ScopedSharedLock : public ScopedBaseLock {
 public:
  explicit ScopedSharedLock(LockHandler *lock) SHARED_LOCK_FUNCTION(lock)
      : ScopedBaseLock(lock, SCOPED_SHARED_LOCK) {}

  ~ScopedSharedLock() UNLOCK_FUNCTION() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(ScopedSharedLock);
};

}  // namespace lmctfy
}  // namespace containers

#endif  // SRC_LOCK_HANDLER_H_
