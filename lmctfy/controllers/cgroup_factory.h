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

#ifndef SRC_CONTROLLERS_CGROUP_FACTORY_H_
#define SRC_CONTROLLERS_CGROUP_FACTORY_H_

#include <map>
#include <string>
using ::std::string;

#include "base/macros.h"
#include "system_api/kernel_api.h"
#include "include/lmctfy.pb.h"
#include "util/safe_types/unix_gid.h"
#include "util/safe_types/unix_uid.h"
#include "util/task/statusor.h"

namespace containers {
namespace lmctfy {

typedef ::system_api::KernelAPI KernelApi;

// Factory for creating valid cgroup paths of a specified resource.
//
// Class is thread-safe.
class CgroupFactory {
 public:
  // Creates a new instance of CgroupFactory and detects the mounted and
  // accessible cgroup hierarchies. Does not take ownership of kernel.
  static ::util::StatusOr<CgroupFactory *> New(const KernelApi *kernel);

  virtual ~CgroupFactory() {}

  // Gets the full cgroup path of the specified type and hierarchy_path. Returns
  // OK with the path iff the path now exists and is ready for use.
  virtual ::util::StatusOr<string> Get(CgroupHierarchy type,
                                       const string &hierarchy_path) const;

  // Creates and returns the full cgroup path of the specified type and
  // hierarchy_path.
  //
  // Arguments:
  //   type: The cgroup hierarchy to create a hierarchy in.
  //   hierarchy_path: The path inside the cgroup hierarchy to create. e.g.:
  //       /alloc/task.
  //   uid: The UnixUid of the user to chown the new cgroup to.
  //   gid: The UnixGid of the group to chown the new cgroup to.
  // Return:
  //   Status or the operation. On success returns OK and populates the full
  //       cgroup path which now exists and is ready for use.
  virtual ::util::StatusOr<string> Create(
      CgroupHierarchy type, const string &hierarchy_path,
      ::util::UnixUid uid, ::util::UnixGid gid) const;

  // Mounts the specified cgroup hierarchies to the specified mount path.
  virtual ::util::Status Mount(const CgroupMount &cgroup);

  // Determines whether the specified hierarchy is mounted on the system.
  virtual bool IsMounted(CgroupHierarchy type) const;

  // Determines whether the specified hierarchy owns its underlying cgroup
  // mount. Return false if the hierarchy type is not known or not supported.
  // This function should only be called on hierarchies that IsMounted() return
  // true.
  virtual bool OwnsCgroup(CgroupHierarchy type) const;

  // Gets the name of the specified hierarchy. An empty string is returned if
  // there is no such hierarchy. If IsMounted() returns true, a non-empty string
  // is guaranteed to be returned from this function.
  virtual string GetHierarchyName(CgroupHierarchy hierarchy) const;

 protected:
  // Arguments:
  //   cgroup_mounts: Map of hierarchy type to its mount path.
  //   kernel: Wrapper for all kernel calls. Does not take ownership.
  CgroupFactory(const ::std::map<CgroupHierarchy, string> &cgroup_mounts,
                const KernelApi *kernel);

 private:
  // Get the cgroup path for the specified cgroup hierarchy and hierarchy_path.
  ::util::StatusOr<string> GetCgroupPath(CgroupHierarchy hierarchy,
                                         const string &hierarchy_path) const;

  // Data object for a mount point path and whether it is owned.
  class MountPoint {
   public:
    MountPoint(const string &path, bool owns) : path(path), owns(owns) {}

    // The absolute mount to the mount point.
    const string path;

    // Whether this mount path is owned.
    const bool owns;
  };

  // Map of hierarchy type to its mount point (a path and whether the hierarchy
  // owns that mount path).
  ::std::map<CgroupHierarchy, MountPoint> mount_paths_;

  // Wrapper for all calls to the kernel.
  const KernelApi *kernel_;

  friend class CgroupFactoryTest;

  DISALLOW_COPY_AND_ASSIGN(CgroupFactory);
};

}  // namespace lmctfy
}  // namespace containers

#endif  // SRC_CONTROLLERS_CGROUP_FACTORY_H_
