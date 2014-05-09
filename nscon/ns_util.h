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

#ifndef PRODUCTION_CONTAINERS_NSCON_NS_UTIL_H_
#define PRODUCTION_CONTAINERS_NSCON_NS_UTIL_H_

#include <sys/types.h>  // for pid_t

#include <map>
#include <set>
#include <string>
using ::std::string;
#include <vector>

#include "base/macros.h"
#include "system_api/libc_fs_api.h"
#include "util/task/status.h"
#include "util/task/statusor.h"

namespace containers {
namespace nscon {

// SavedNamespace
// Remembers the current process's namespace (identified by namespace flag and
// an open FD on the namespace file) at creation time. It then setns() to that
// open FD when RestoreAndDelete() is called.
// Typical usage for this class ls:
//
//    unique_ptr<SavedNamespace> saved_ns = new SavedNamespace(ns, fd);
//    // Namespace now remembered.
//    ...
//    // Switch to some other namespace
//    ...
//    saved_ns->RestoreAndDelete();  // Switches back to original namespace.
//    saved_ns.release();
//
// This class is thread-hostile since it calls setns() which may not be invoked
// from a multi-threaded process.
class SavedNamespace {
 public:
  virtual ~SavedNamespace() {}

  // Switches the namespace of the current process back to what was stored.
  // If successful, takes ownership of this SavedNamespace object and
  // destroys it.
  virtual ::util::Status RestoreAndDelete();

 protected:
  SavedNamespace(int ns, int fd) : ns_(ns), fd_(fd), fd_closer_(fd) {}

 private:
  int ns_;
  int fd_;
  ::system_api::ScopedFileCloser fd_closer_;

  friend class MockSavedNamespace;
  friend class NsUtil;
  friend class NsUtilTest;
  DISALLOW_COPY_AND_ASSIGN(SavedNamespace);
};

// Collection of common util functions for namespace controller
class NsUtil {
 public:
  static ::util::StatusOr<NsUtil *> New();

  virtual ~NsUtil() {}

  // Attaches to the namespace jail of process with pid |target|.  The
  // |namespaces| is a vector of CLONE_* flags indicating which namespaces
  // the caller wants to attach to.
  // The format of clone flags is the same as that used in clone(2) wrapper.
  // Returns status of the operation, OK iff successful.
  virtual ::util::Status AttachNamespaces(const ::std::vector<int>& namespaces,
                                          pid_t target) const;

  // Creates new namespace jail.  |namespaces| is a vector of CLONE_ flags that
  // indicates which new namespaces will be created.
  // Returns status of the operation, OK iff successful.
  virtual ::util::Status UnshareNamespaces(
      const ::std::vector<int>& namespaces) const;

  // Returns string representation of namespace indicated by the |clone_flag|.
  virtual ::util::StatusOr<const char*> NsCloneFlagToName(int clone_flag) const;

  // Gets the list of namespaces that the current process has unshared with
  // respect to the given pid.
  virtual ::util::StatusOr<const ::std::vector<int>> GetUnsharedNamespaces(
      pid_t pid) const;

  // Returns 'true' if the namespace is supported by the kernel, else 'false'.
  virtual bool IsNamespaceSupported(int ns) const;

  // Reads the namespace-id embedded in the namespace symlink file.
  // The symlink data looks like:
  //    ls -l /proc/self/ns/ipc
  //    lrwxrwxrwx 1 root root 0 2014-02-10 12:06 ipc -> ipc:[4026531839]
  // From above example, "ipc:[4026531839]" is retrieved and returned.
  // Pid of 0 indicates current process.
  virtual ::util::StatusOr<string> GetNamespaceId(pid_t pid, int ns) const;

  // Returns a SavedNamespace object which remembers the 'ns' for current
  // process and can be restored later. This function acts as a factory for
  // SavedNamespace class. Caller takes ownership of the returned object.
  virtual ::util::StatusOr<SavedNamespace *> SaveNamespace(int ns) const;

  // Checks if a character device file exists at 'path'.
  // Arguments:
  //   path: The absolute path to the character device file.
  // Returns:
  //   OK: Iff a character device file exists at 'path'.
  //   INVALID_ARGUMENT: If 'path' does not exist or does not point to a
  //                     character device file.
  //   INTERNAL: If any syscall fails.
  // MODE:begin_strip
  // TODO(vishnuk): Delete this method and use
  // util::FsUtils::FileExists once it can support checking of
  // specific file types.
  // MODE:end_strip
  virtual ::util::Status CharacterDeviceFileExists(
      const string &path) const;

  // Dups stdin, stdout and stderr to fd and closes fd on success.
  // Arguments:
  //   console_fd: The fd that points to the console.
  // Returns:
  //   OK: Iff successful.
  virtual ::util::Status AttachToConsoleFd(const int console_fd) const;

  // Opens slave pty device represented by slave_pty and returns the fd on
  // success.
  // Arguments:
  //     slave_pty: Slave pty device number.
  // Returns:
  //     fd iff successful.
  virtual ::util::StatusOr<int> OpenSlavePtyDevice(
      const string &slave_pty) const;

  // Returns the list of currently open FDs. This function opens
  // '/proc/self/fd/' directory and parses the FDs from it.
  virtual ::util::StatusOr<::std::vector<int>> GetOpenFDs() const;

 protected:
  explicit NsUtil(::std::set<int> supported_namespaces)
      : supported_namespaces_(supported_namespaces) {}

 private:
  ::util::Status DupToFd(int oldfd, int newfd) const;

  // Namespaces supported by the kernel we are running on.
  ::std::set<int> supported_namespaces_;

  friend class NsUtilTest;
  DISALLOW_COPY_AND_ASSIGN(NsUtil);
};

// Exposed here for testing purposes only.
namespace internal {
void InitKnownNamespaces();
}  // namespace internal

}  // namespace nscon
}  // namespace containers

#endif  // PRODUCTION_CONTAINERS_NSCON_NS_UTIL_H_
