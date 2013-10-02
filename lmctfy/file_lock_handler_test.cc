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
#include <sys/stat.h>
#include <unistd.h>
#include <memory>
#include <vector>

#include "base/callback.h"
#include "gflags/gflags.h"
#include "system_api/kernel_api_mock.h"
#include "file/base/path.h"
#include "include/lmctfy.pb.h"
#include "strings/stringpiece.h"
#include "strings/substitute.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "thread/thread.h"
#include "thread/thread_options.h"
#include "util/gtl/stl_util.h"
#include "util/task/codes.pb.h"

DECLARE_string(test_tmpdir);

using ::system_api::KernelAPIMock;
using ::file::JoinPath;
using ::std::unique_ptr;
using ::std::vector;
using ::strings::Substitute;
using ::testing::Return;
using ::testing::StrictMock;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace lmctfy {
namespace {

// Checks whether the specified file exists
bool FileExists(const string &file_path) {
  struct stat unused;
  return stat(file_path.c_str(), &unused) == 0;
}

// Creates the specified file. File is empty.
void MakeFile(const string &file_path) {
  ASSERT_EQ(0, mknod(file_path.c_str(), 0777 | S_IFREG, 0));
}

// Creates the specified directory.
void MakeDir(const string &file_path) {
  ASSERT_EQ(0, mkdir(file_path.c_str(), 0777));
}


TEST(Close, AllTests) {
  Close c;
  const string kName = JoinPath(FLAGS_test_tmpdir, "tmp");
  int fd;
  string fd_proc_path;

  // Create the file.
  MakeFile(kName);
  ASSERT_TRUE(FileExists(kName));

  // File is already deleted when cleanup is called.
  fd = open(kName.c_str(), O_RDONLY);
  ASSERT_LE(0, fd);
  fd_proc_path = Substitute("/proc/self/fd/$0", fd);

  unlink(kName.c_str());
  ASSERT_TRUE(FileExists(fd_proc_path));
  c.Cleanup(fd);
  EXPECT_FALSE(FileExists(fd_proc_path));
  EXPECT_FALSE(FileExists(kName));

  // Create the file.
  MakeFile(kName);
  ASSERT_TRUE(FileExists(kName));

  // Success
  fd = open(kName.c_str(), O_RDONLY);
  ASSERT_LE(0, fd);
  fd_proc_path = Substitute("/proc/self/fd/$0", fd);

  ASSERT_TRUE(FileExists(fd_proc_path));
  c.Cleanup(fd);
  EXPECT_FALSE(FileExists(fd_proc_path));
  EXPECT_TRUE(FileExists(kName));

  unlink(kName.c_str());
}

TEST(Unlink, AllTests) {
  Unlink u;
  const string kName = JoinPath(FLAGS_test_tmpdir, "tmp");

  // Success
  MakeFile(kName);
  ASSERT_TRUE(FileExists(kName));
  u.Cleanup(kName);
  EXPECT_FALSE(FileExists(kName));

  // File does not exist
  u.Cleanup(kName);
  EXPECT_FALSE(FileExists(kName));
}

class FileLockHandlerFactoryTest : public ::testing::Test {
 public:
  FileLockHandlerFactoryTest()
      : kContainerName("/test"),
        kLockFilePath(JoinPath(FLAGS_test_tmpdir, kContainerName + ".lock")),
        kLockDirPath(JoinPath(FLAGS_test_tmpdir, kContainerName)),
        kRootLockFilePath(JoinPath(FLAGS_test_tmpdir, ".lock")) {
  }

  virtual void SetUp() {
    // Cleanup the container
    unlink(kLockFilePath.c_str());
    rmdir(kLockDirPath.c_str());
    unlink(kRootLockFilePath.c_str());

    mock_kernel_.reset(new StrictMock<KernelAPIMock>());
    factory_.reset(
        new FileLockHandlerFactory(FLAGS_test_tmpdir, mock_kernel_.get()));

    ASSERT_FALSE(FileExists(kLockFilePath));
    ASSERT_FALSE(FileExists(kLockDirPath));
  }

  void ExpectLocksExist(bool lock_file_exists, bool lock_dir_exists) {
    EXPECT_EQ(lock_file_exists, FileExists(kLockFilePath));
    EXPECT_EQ(lock_dir_exists, FileExists(kLockDirPath));
  }

 protected:
  const string kContainerName;
  const string kLockFilePath;
  const string kLockDirPath;
  const string kRootLockFilePath;
  unique_ptr<FileLockHandlerFactory> factory_;
  unique_ptr<KernelAPIMock> mock_kernel_;
};

TEST_F(FileLockHandlerFactoryTest, CreateSuccess) {
  StatusOr<LockHandler *> statusor = factory_->Create(kContainerName);
  ASSERT_TRUE(statusor.ok());
  EXPECT_NE(nullptr, statusor.ValueOrDie());

  ExpectLocksExist(true, true);
  delete statusor.ValueOrDie();
}

TEST_F(FileLockHandlerFactoryTest, CreateSubcontainerSuccess) {
  // Create parent
  StatusOr<LockHandler *> statusor = factory_->Create(kContainerName);
  ASSERT_TRUE(statusor.ok());
  EXPECT_NE(nullptr, statusor.ValueOrDie());

  ExpectLocksExist(true, true);
  delete statusor.ValueOrDie();

  // Create a subcontainer
  statusor = factory_->Create(JoinPath(kContainerName, "sub"));
  ASSERT_TRUE(statusor.ok());
  EXPECT_NE(nullptr, statusor.ValueOrDie());

  // Cleanup the container
  EXPECT_TRUE(statusor.ValueOrDie()->Destroy().ok());
}

TEST_F(FileLockHandlerFactoryTest, CreateNonExistentSubcontainer) {
  // Create parent
  StatusOr<LockHandler *> statusor = factory_->Create(kContainerName);
  ASSERT_TRUE(statusor.ok());
  EXPECT_NE(nullptr, statusor.ValueOrDie());

  ExpectLocksExist(true, true);
  delete statusor.ValueOrDie();

  // Create a subcontainer
  statusor = factory_->Create(JoinPath(kContainerName, "sub", "other"));
  ASSERT_FALSE(statusor.ok());
}

TEST_F(FileLockHandlerFactoryTest, CreateLockFileAlreadyExists) {
  MakeFile(kLockFilePath);

  StatusOr<LockHandler *> statusor = factory_->Create(kContainerName);
  ASSERT_FALSE(statusor.ok());
  EXPECT_EQ(::util::error::FAILED_PRECONDITION, statusor.status().error_code());
  ExpectLocksExist(true, false);
}

TEST_F(FileLockHandlerFactoryTest, CreateLockDirAlreadyExists) {
  MakeDir(kLockDirPath);

  StatusOr<LockHandler *> statusor = factory_->Create(kContainerName);
  ASSERT_FALSE(statusor.ok());
  EXPECT_EQ(::util::error::FAILED_PRECONDITION, statusor.status().error_code());
  ExpectLocksExist(false, true);
}

TEST_F(FileLockHandlerFactoryTest, GetSuccess) {
  // Create the lock.
  StatusOr<LockHandler *> statusor = factory_->Create(kContainerName);
  ASSERT_TRUE(statusor.ok());
  delete statusor.ValueOrDie();

  statusor = factory_->Get(kContainerName);
  ASSERT_TRUE(statusor.ok());
  EXPECT_NE(nullptr, statusor.ValueOrDie());

  ExpectLocksExist(true, true);
  delete statusor.ValueOrDie();
}

TEST_F(FileLockHandlerFactoryTest, GetLockFileDoesNotExist) {
  StatusOr<LockHandler *> statusor = factory_->Get(kContainerName);
  ASSERT_FALSE(statusor.ok());
  EXPECT_EQ(::util::error::NOT_FOUND, statusor.status().error_code());
  ExpectLocksExist(false, false);
}

TEST_F(FileLockHandlerFactoryTest, GetLockDirectoryDoesNotExist) {
  // Create the lock.
  StatusOr<LockHandler *> statusor = factory_->Create(kContainerName);
  ASSERT_TRUE(statusor.ok());
  delete statusor.ValueOrDie();

  // Delete the lock directory.
  rmdir(kLockDirPath.c_str());

  // Get the container, we don't actually fail if the lock directory is missing.
  statusor = factory_->Get(kContainerName);
  ASSERT_TRUE(statusor.ok());
  EXPECT_NE(nullptr, statusor.ValueOrDie());

  ExpectLocksExist(true, false);
  delete statusor.ValueOrDie();
}


TEST_F(FileLockHandlerFactoryTest, InitAlreadyMounted) {
  InitSpec spec;

  EXPECT_CALL(*mock_kernel_, FileExists(kRootLockFilePath))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_kernel_, MkDirRecursive(FLAGS_test_tmpdir))
      .WillRepeatedly(Return(0));

  EXPECT_TRUE(factory_->InitMachine(spec).ok());
}

TEST_F(FileLockHandlerFactoryTest, InitCreateLockFile) {
  InitSpec spec;

  EXPECT_CALL(*mock_kernel_, FileExists(kRootLockFilePath))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*mock_kernel_, MkDirRecursive(FLAGS_test_tmpdir))
      .WillRepeatedly(Return(0));
  ASSERT_FALSE(FileExists(kRootLockFilePath));

  EXPECT_TRUE(factory_->InitMachine(spec).ok());
  EXPECT_TRUE(FileExists(kRootLockFilePath));

  unlink(kRootLockFilePath.c_str());
}

TEST_F(FileLockHandlerFactoryTest, InitMkDirFails) {
  InitSpec spec;

  EXPECT_CALL(*mock_kernel_, FileExists(kRootLockFilePath))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_kernel_, MkDirRecursive(FLAGS_test_tmpdir))
      .WillRepeatedly(Return(-1));

  EXPECT_EQ(::util::error::FAILED_PRECONDITION,
            factory_->InitMachine(spec).error_code());
}

TEST_F(FileLockHandlerFactoryTest, InitCreateLockFileFails) {
  InitSpec spec;

  EXPECT_CALL(*mock_kernel_, FileExists(kRootLockFilePath))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*mock_kernel_, MkDirRecursive(FLAGS_test_tmpdir))
      .WillRepeatedly(Return(0));

  // We say the file does not exist, but we place it on the file system so that
  // the exclusive open fails.
  MakeFile(kRootLockFilePath);

  EXPECT_EQ(::util::error::FAILED_PRECONDITION,
            factory_->InitMachine(spec).error_code());

  unlink(kRootLockFilePath.c_str());
}

class FileLockHandlerTest : public ::testing::Test {
 public:
  FileLockHandlerTest()
      : kLockFilePath(JoinPath(FLAGS_test_tmpdir, "test.lock")),
        kLockDirPath(JoinPath(FLAGS_test_tmpdir, "test")) {
  }

  virtual void SetUp() {
    // Cleanup the container
    unlink(kLockFilePath.c_str());
    rmdir(kLockDirPath.c_str());

    // Create the initial files
    MakeFile(kLockFilePath);
    MakeDir(kLockDirPath);

    int lock_fd = open(kLockFilePath.c_str(), O_RDONLY);
    ASSERT_LE(0, lock_fd);
    lock_.reset(new FileLockHandler(lock_fd, kLockFilePath, kLockDirPath,
                                    false));

    ASSERT_TRUE(FileExists(kLockFilePath));
    ASSERT_TRUE(FileExists(kLockDirPath));
  }

  void ExpectLocksExist(bool lock_file_exists, bool lock_dir_exists) {
    EXPECT_EQ(lock_file_exists, FileExists(kLockFilePath));
    EXPECT_EQ(lock_dir_exists, FileExists(kLockDirPath));
  }

 protected:
  const string kLockFilePath;
  const string kLockDirPath;
  unique_ptr<FileLockHandler> lock_;
};

TEST_F(FileLockHandlerTest, DestroySuccess) {
  EXPECT_TRUE(lock_.release()->Destroy().ok());
  ExpectLocksExist(false, false);
}

TEST_F(FileLockHandlerTest, DestroyLockFileDoesNotExist) {
  // Delete the lock file.
  unlink(kLockFilePath.c_str());

  // Should still succeed.
  Status status = lock_.release()->Destroy();
  EXPECT_TRUE(status.ok());
  ExpectLocksExist(false, false);
}

TEST_F(FileLockHandlerTest, DestroyLockDirectoryDoesNotExist) {
  // Delete the lock directory.
  rmdir(kLockDirPath.c_str());

  // Should still succeed.
  Status status = lock_.release()->Destroy();
  EXPECT_TRUE(status.ok());
  ExpectLocksExist(false, false);
}

TEST_F(FileLockHandlerTest, DestroyLockDirectoryIsNotEmpty) {
  const string kDummyFile = JoinPath(kLockDirPath, "dummy_file");
  // Create a file in the lock directory.
  MakeFile(kDummyFile);

  Status status = lock_->Destroy();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(::util::error::FAILED_PRECONDITION, status.error_code());
  ExpectLocksExist(true, true);

  unlink(kDummyFile.c_str());
}

TEST_F(FileLockHandlerTest, LockExclusive) {
  EXPECT_TRUE(lock_->ExclusiveLock().ok());

  lock_->Unlock();
}

TEST_F(FileLockHandlerTest, LockSharedSingleUser) {
  EXPECT_TRUE(lock_->SharedLock().ok());

  lock_->Unlock();
}


// No thread-safety analysis since this is a case where the lock fails and
// thread-safety analysis doesn't handle that well.
TEST_F(FileLockHandlerTest, LockLockfileDoesNotExist)
    NO_THREAD_SAFETY_ANALYSIS {
  unlink(kLockFilePath.c_str());

  // Exclusive
  Status status = lock_->ExclusiveLock();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(::util::error::NOT_FOUND, status.error_code());

  // Shared
  status = lock_->ExclusiveLock();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(::util::error::NOT_FOUND, status.error_code());
}

TEST_F(FileLockHandlerTest, UnlockExclusiveNoLockfile) {
  // Grab exclusive lock.
  EXPECT_TRUE(lock_->ExclusiveLock().ok());

  // Delete lockfile
  unlink(kLockFilePath.c_str());

  // Unlock should not fail
  lock_->Unlock();
}

TEST_F(FileLockHandlerTest, UnlockSharedNoLockFile) {
  // Grab shared lock.
  EXPECT_TRUE(lock_->SharedLock().ok());

  // Delete lockfile
  unlink(kLockFilePath.c_str());

  // Unlock should not fail
  lock_->Unlock();
}

TEST_F(FileLockHandlerTest, ScopedExclusiveLockSuccess) {
  ScopedExclusiveLock l(lock_.get());
  EXPECT_TRUE(l.lock_status().ok());
}

TEST_F(FileLockHandlerTest, ScopedExclusiveLockNoLockfile) {
  unlink(kLockFilePath.c_str());

  ScopedExclusiveLock l(lock_.get());
  EXPECT_FALSE(l.lock_status().ok());
}

TEST_F(FileLockHandlerTest, ScopedSharedLockSuccess) {
  ScopedSharedLock l(lock_.get());
  EXPECT_TRUE(l.lock_status().ok());
}

TEST_F(FileLockHandlerTest, ScopedSharedLockNoLockfile) {
  unlink(kLockFilePath.c_str());

  ScopedSharedLock l(lock_.get());
  EXPECT_FALSE(l.lock_status().ok());
}

class RootFileLockHandlerTest : public ::testing::Test {
 public:
  RootFileLockHandlerTest()
      : kLockFilePath(JoinPath(FLAGS_test_tmpdir, ".lock")),
        kLockDirPath(FLAGS_test_tmpdir) {}

  virtual void SetUp() {
    // Cleanup the container
    unlink(kLockFilePath.c_str());

    // Create the lockfile
    MakeFile(kLockFilePath);

    int lock_fd = open(kLockFilePath.c_str(), O_RDONLY);
    ASSERT_LE(0, lock_fd);
    lock_.reset(new FileLockHandler(lock_fd, kLockFilePath, kLockDirPath,
                                    true));

    ASSERT_TRUE(FileExists(kLockFilePath));
    ASSERT_TRUE(FileExists(kLockDirPath));
  }

 protected:
  const string kLockFilePath;
  const string kLockDirPath;
  unique_ptr<FileLockHandler> lock_;
};

TEST_F(RootFileLockHandlerTest, ExclusiveLockUnlock) {
  EXPECT_TRUE(lock_->ExclusiveLock().ok());

  lock_->Unlock();
}

TEST_F(RootFileLockHandlerTest, SharedLockUnlock) {
  EXPECT_TRUE(lock_->SharedLock().ok());

  lock_->Unlock();
}

TEST_F(RootFileLockHandlerTest, Destroy) {
  Status status = lock_->Destroy();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(::util::error::PERMISSION_DENIED, status.error_code());

  // Ensure the lockfiles are still around.
  EXPECT_TRUE(FileExists(kLockFilePath));
  EXPECT_TRUE(FileExists(kLockDirPath));
}

}  // namespace
}  // namespace lmctfy
}  // namespace containers
