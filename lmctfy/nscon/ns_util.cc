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
// Common util functions for namespace controller
//

#include "nscon/ns_util.h"

#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "base/mutex.h"
#include "file/base/path.h"
#include "util/errors.h"
#include "util/scoped_cleanup.h"
#include "system_api/libc_fs_api.h"
#include "system_api/libc_process_api.h"
#include "strings/substitute.h"
#include "util/gtl/lazy_static_ptr.h"

using ::file::JoinPath;
using ::std::map;
using ::std::set;
using ::std::vector;
using ::strings::Substitute;
using ::system_api::GlobalLibcFsApi;
using ::system_api::GlobalLibcProcessApi;
using ::util::ScopedCleanup;
using ::util::error::INTERNAL;
using ::util::error::INVALID_ARGUMENT;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace nscon {

// Namespaces that we currently know exist (irrespective of whether the kernel
// supports them or not).
static ::util::gtl::LazyStaticPtr<map<int, string>> g_known_namespaces;
// Used to protect initialization of g_known_namespaces.
static Mutex g_known_namespaces_init_lock(::base::LINKER_INITIALIZED);

void internal::InitKnownNamespaces() {
  MutexLock m(&g_known_namespaces_init_lock);

  // Initialize only once.
  if (g_known_namespaces->empty()) {
    *g_known_namespaces = { { CLONE_NEWUSER, "user" },
                            { CLONE_NEWPID, "pid" },
                            { CLONE_NEWNS, "mnt" },
                            { CLONE_NEWIPC, "ipc" },
                            { CLONE_NEWNET, "net" },
                            { CLONE_NEWUTS, "uts" } };
  }
}

StatusOr<NsUtil *> NsUtil::New() {
  internal::InitKnownNamespaces();
  set<int> namespaces;
  // Figure out what namespaces are supported by the kernel.
  for (auto ns_flag_name_pair : *g_known_namespaces) {
    string ns_path = JoinPath("/proc/self/ns/", ns_flag_name_pair.second);
    // Check if this ns file exists. If it does, assume the namespace is
    // supported in the kernel.
    struct stat statbuf;
    if (GlobalLibcFsApi()->LStat(ns_path.c_str(), &statbuf) == 0) {
      namespaces.insert(ns_flag_name_pair.first);
    }
  }

  // NOTE: In future, we may also want to check kernel version number too.
  return new NsUtil(namespaces);
}

struct ScopedFdListCloser : public ScopedCleanup {
 public:
  explicit ScopedFdListCloser(vector<int>* fd_list)
      : ScopedCleanup(&ScopedFdListCloser::Close, fd_list) {}

  static void Close(vector<int>* fd_list) {
    for (auto fd : *fd_list) {
      GlobalLibcFsApi()->Close(fd);
    }
  }
};

bool NsUtil::IsNamespaceSupported(int ns) const {
  return supported_namespaces_.find(ns) != supported_namespaces_.end();
}

StatusOr<const char*> NsUtil::NsCloneFlagToName(int clone_flag) const {
  auto it = g_known_namespaces->find(clone_flag);
  if (it == g_known_namespaces->end()) {
    return Status(INVALID_ARGUMENT,
                  Substitute("Unknown namespace flag '$0'", clone_flag));
  }

  return it->second.c_str();
}

Status NsUtil::AttachNamespaces(
    const vector<int>& namespaces, pid_t target) const {
  if (0 == target) {
    return {INVALID_ARGUMENT,
          Substitute("Invalid target PID '$0'", target)};
  }

  if (namespaces.empty()) {
    // No namespaces to attach
    return Status::OK;
  }

  vector<int> fd_list;
  // Make sure that all the FDs are closed on error.
  ScopedFdListCloser fd_closer(&fd_list);

  for (auto ns_flag : namespaces) {
    const char* ns_file_name = RETURN_IF_ERROR(NsCloneFlagToName(ns_flag));

    // Use raw Open() instead of FOpen() since FOpen() will resolve the symlink
    // and namespace symlinks point to a non-existant target.
    const string& filename =
        JoinPath("/proc", SimpleItoa(target), "ns", ns_file_name);
    int fd = GlobalLibcFsApi()->Open(filename.c_str(), O_RDONLY);
    if (fd < 0) {
      return {INTERNAL,
                    Substitute("AttachNamespaces Failed: Open($0): $1",
                               filename, StrError(errno))};
    }

    // Store userns FD at the front as we must attach to it first before
    // attaching to any other namespaces.
    if (ns_flag == CLONE_NEWUSER) {
      fd_list.insert(fd_list.begin(), fd);
    } else {
      fd_list.push_back(fd);
    }
  }

  for (auto fd : fd_list) {
    if (GlobalLibcProcessApi()->Setns(fd, 0) < 0) {
      return {INTERNAL, Substitute("AttachNamespaces Failed: Setns(): $0",
                                   StrError(errno))};
    }
  }
  // Close the FDs and check its return value. We pop_back() the FDs as we do
  // not want fd_closer() to close the same FD multiple times in case something
  // goes wrong here.
  while (!fd_list.empty()) {
    int fd = fd_list.back();
    fd_list.pop_back();
    if (GlobalLibcFsApi()->Close(fd) < 0) {
      return {INTERNAL, Substitute("AttachNamespaces Failed: Close(): $0",
                                   StrError(errno))};
    }
  }

  return Status::OK;
}

Status NsUtil::UnshareNamespaces(const vector<int> &namespaces) const {
  int unshare_flags = 0;

  for (auto ns_flag : namespaces) {
    // Use NsCloneFlagToName() to check for the validity of the ns_flag.
    RETURN_IF_ERROR(NsCloneFlagToName(ns_flag));
    unshare_flags |= ns_flag;
  }

  if (unshare_flags == 0) {
    // Nothing to unshare.
    return Status::OK;
  }

  if (GlobalLibcProcessApi()->Unshare(unshare_flags) < 0) {
    return {INTERNAL, Substitute("unshare failed: $0", StrError(errno))};
  }

  return Status::OK;
}

StatusOr<string> NsUtil::GetNamespaceId(pid_t pid, int ns) const {
  if (pid < 0) {
    return Status(INVALID_ARGUMENT, Substitute("Invalid pid $0", pid));
  }

  const char *ns_name = RETURN_IF_ERROR(NsCloneFlagToName(ns));
  string ns_path;
  if (pid == 0) {
    ns_path = JoinPath("/proc/self/ns/", ns_name);
  } else {
    ns_path = JoinPath("/proc", SimpleItoa(pid), "ns", ns_name);
  }

  char linkdata[64];
  memset(linkdata, 0, sizeof(linkdata));
  if (GlobalLibcFsApi()->ReadLink(ns_path.c_str(), linkdata,
                                  sizeof(linkdata)) < 0) {
    return Status(INTERNAL,
                  Substitute("readlink($0) failed: $1",
                             ns_path, StrError(errno)));
  }

  return string(linkdata);
}

StatusOr<const vector<int>> NsUtil::GetUnsharedNamespaces(pid_t pid) const {
  if (pid <= 0) {
    return Status(INVALID_ARGUMENT, Substitute("Invalid pid $0", pid));
  }

  // Find out what namespaces we are in.
  set<string> current_namespaces;
  for (auto ns : supported_namespaces_) {
    current_namespaces.insert(RETURN_IF_ERROR(GetNamespaceId(0, ns)));
  }

  // Now compare our namespaces with target's
  vector<int> namespaces;
  for (auto ns : supported_namespaces_) {
    const string ns_id = RETURN_IF_ERROR(GetNamespaceId(pid, ns));
    // If the namespace is not same as ours, its unshared.
    if (current_namespaces.find(ns_id) == current_namespaces.end()) {
      namespaces.push_back(ns);
    }
  }

  return namespaces;
}

Status NsUtil::CharacterDeviceFileExists(const string &path) const {
  struct stat stat_buf;
  if (GlobalLibcFsApi()->Stat(path.c_str(), &stat_buf) != 0) {
    if (errno != ENOENT) {
      return {INTERNAL,
          Substitute("Failed to stat character device: $0. Error: $1",
                     path, StrError(errno))};
    } else {
      return {INVALID_ARGUMENT,
            Substitute("Character device missing: $0", path)};
    }
  }
  if (!S_ISCHR(stat_buf.st_mode)) {
    return {INVALID_ARGUMENT,
          Substitute("$0 is not a character device file.", path)};
  }
  return Status::OK;
}

// Open namespace file and remember its fd.
StatusOr<SavedNamespace *> NsUtil::SaveNamespace(int ns) const {
  const string ns_file = JoinPath("/proc/self/ns",
                                  RETURN_IF_ERROR(NsCloneFlagToName(ns)));
  int fd = GlobalLibcFsApi()->Open(ns_file.c_str(), O_RDONLY);
  if (fd < 0) {
    return Status(INTERNAL,
                  Substitute("Failed to save namespace: open($0) failed: $1",
                             ns_file, StrError(errno)));
  }

  return new SavedNamespace(ns, fd);
}

// Setns() to the namespace FD and then close it.
Status SavedNamespace::RestoreAndDelete() {
  if (GlobalLibcProcessApi()->Setns(fd_, 0) < 0) {
    return {INTERNAL,
                  Substitute("RestoreAndDelete: setns() failed: $0",
                             StrError(errno))};
  }

  if (GlobalLibcFsApi()->Close(fd_) < 0) {
    return {INTERNAL,
                  Substitute("RestoreAndDelete: close() failed: $0",
                             StrError(errno))};
  }

  fd_closer_.Cancel();
  delete this;
  return Status::OK;
}

Status NsUtil::DupToFd(int oldfd, int newfd) const {
  if (GlobalLibcFsApi()->Dup2(oldfd, newfd) < 0) {
    return {INTERNAL,
          Substitute("Failed to dup fd $0 to fd $1. Error: $2",
                     oldfd, newfd, StrError(errno))};
  }
  return Status::OK;
}

StatusOr<int> NsUtil::OpenSlavePtyDevice(const string &slave_pty) const {
  const string &slave_pty_path = file::JoinPath("/dev/pts", slave_pty);
  RETURN_IF_ERROR(CharacterDeviceFileExists(slave_pty_path));
  int fd = GlobalLibcFsApi()->Open(slave_pty_path.c_str(), O_RDWR);
  if (fd == -1) {
    return Status(INTERNAL,
                  Substitute("Failed to open slave pty $0. Error: $1",
                             slave_pty_path, StrError(errno)));
  }
  return fd;
}

Status NsUtil::AttachToConsoleFd(const int console_fd) const {
  RETURN_IF_ERROR(DupToFd(console_fd, 0));
  RETURN_IF_ERROR(DupToFd(console_fd, 1));
  RETURN_IF_ERROR(DupToFd(console_fd, 2));
  // Acquire controlling tty on BSD.
#ifdef TIOCSCTTY
  if (GlobalLibcFsApi()->Ioctl(console_fd, TIOCSCTTY, 0) != 0) {
    return {INTERNAL,
          Substitute("Failed to attach to console fd $0. Error: $1",
                     console_fd, StrError(errno))};
  }
#endif

  if (console_fd > STDERR_FILENO) {
    if (GlobalLibcFsApi()->Close(console_fd) < 0) {
      return {INTERNAL,
            Substitute("Failed to close slave pty fd: $0. Error: $1",
                       console_fd, StrError(errno))};
    }
  }

  return Status::OK;
}

struct ScopedDirCloser : public ScopedCleanup {
 public:
  explicit ScopedDirCloser(DIR *dir)
      : ScopedCleanup(&ScopedDirCloser::CloseDir, dir) {}

  static void CloseDir(DIR *dir) {
    GlobalLibcFsApi()->CloseDir(dir);
  }
};

StatusOr<vector<int>> NsUtil::GetOpenFDs() const {
  vector<int> fd_list;
  const string dirpath = "/proc/self/fd/";
  DIR *dir = GlobalLibcFsApi()->OpenDir(dirpath.c_str());
  if (dir == nullptr) {
    return Status(INTERNAL,
                  Substitute("opendir($0): $1", dirpath, StrError(errno)));
  }

  ScopedDirCloser dir_closer(dir);
  struct dirent ent;
  struct dirent *result;
  do {
    int ret = GlobalLibcFsApi()->ReadDirR(dir, &ent, &result);
    if (ret != 0) {
      // readdir_r() returns the error code as return value.
      return Status(INTERNAL,
                    Substitute("readdir_r() error: $0", StrError(ret)));
    }
    if (result == nullptr) {
      // Reached the EOF for the directory.
      break;
    }

    // Store the successfully parsed FD in fd_list.
    int fd;
    if (SimpleAtoi(ent.d_name, &fd) == true) {
      fd_list.push_back(fd);
    }
  } while (true);

  return fd_list;
}

}  // namespace nscon
}  // namespace containers
