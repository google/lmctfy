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

#include "lmctfy/controllers/cgroup_factory.h"

#include <unistd.h>
#include <algorithm>
#include <map>
#include <set>
#include <vector>

#include "base/macros.h"
#include "base/mutex.h"
#include "file/base/path.h"
#include "lmctfy/util/proc_cgroup.h"
#include "lmctfy/util/proc_cgroups.h"
#include "util/errors.h"
#include "util/proc_mounts.h"
#include "strings/join.h"
#include "strings/stringpiece.h"
#include "strings/substitute.h"
#include "util/gtl/lazy_static_ptr.h"
#include "util/task/codes.pb.h"
#include "util/task/status.h"

using ::file::JoinPath;
using ::util::ProcMounts;
using ::util::ProcMountsData;
using ::std::make_pair;
using ::std::map;
using ::std::max;
using ::std::set;
using ::std::vector;
using ::strings::Join;
using ::strings::Substitute;
using ::util::Status;
using ::util::StatusOr;
using ::util::gtl::LazyStaticPtr;

namespace containers {
namespace lmctfy {

// The type of mount for cgroup hierarchies.
static const char kCgroupMountType[] = "cgroup";

// Map from hierarchy name to CgroupHierarchy for all supported hierarchies.
static LazyStaticPtr<map<string, CgroupHierarchy>>
    g_supported_hierarchies;
static Mutex g_supported_hierarchies_init_lock(::base::LINKER_INITIALIZED);

namespace internal {

// Safely initialize the supported hierarchies global map.
void InitSupportedHierarchies() {
  MutexLock m(&g_supported_hierarchies_init_lock);

  // Only initialize once.
  if (g_supported_hierarchies->empty()) {
    *g_supported_hierarchies = {
        {"cpu", CGROUP_CPU}, {"cpuacct", CGROUP_CPUACCT},
        {"cpuset", CGROUP_CPUSET}, {"job", CGROUP_JOB},
        {"freezer", CGROUP_FREEZER}, {"memory", CGROUP_MEMORY},
        {"net", CGROUP_NET}, {"blkio", CGROUP_BLOCKIO},
        {"perf_event", CGROUP_PERF_EVENT}, {"rlimit", CGROUP_RLIMIT},
        { "devices", CGROUP_DEVICE}, };
  }
}

}  // namespace internal

// Get the name of all supported hierarchies.
static set<string> GetSupportedHierarchyNames() {
  set<string> result;

  for (const auto &name_hierarchy_pair : *g_supported_hierarchies) {
    result.insert(name_hierarchy_pair.first);
  }

  return result;
}

// Takes a cgroup hierarchy name and returns the corresponding CgroupHierarchy.
// i.e.: "memory" -> CGROUP_MEMORY
static StatusOr<CgroupHierarchy> GetCgroupHierarchy(
    const string &hierarchy_name) {
  auto it = g_supported_hierarchies->find(hierarchy_name);
  if (it == g_supported_hierarchies->end()) {
    return Status(
        ::util::error::INVALID_ARGUMENT,
        Substitute("Unknown cgroup hierarchy \"$0\"", hierarchy_name));
  }

  return it->second;
}

StatusOr<CgroupFactory *> CgroupFactory::New(const KernelApi *kernel) {
  internal::InitSupportedHierarchies();

  // Get the supported cgroup names.
  set<string> supported_cgroups = GetSupportedHierarchyNames();

  // Auto-detect mount points for the cgroup hierarchies.
  map<CgroupHierarchy, string> detected_mounts;
  for (const ProcMountsData &mount : ProcMounts()) {
    // We only care about cgroups.
    if (mount.type != "cgroup") {
      continue;
    }

    // If we can't access the mount point, ignore it.
    if (kernel->Access(mount.mountpoint, R_OK) != 0) {
      continue;
    }

    // Look through the options (they contain the mounted cgroup hierarchies)
    // and add those that we support to the detected mounts.
    for (const string &option : mount.options) {
      if (supported_cgroups.find(option) != supported_cgroups.end()) {
        CgroupHierarchy hierarchy =
            RETURN_IF_ERROR(GetCgroupHierarchy(option));

        detected_mounts.insert(make_pair(hierarchy, mount.mountpoint));
      }
    }
  }

  return new CgroupFactory(detected_mounts, kernel);
}

CgroupFactory::CgroupFactory(const map<CgroupHierarchy, string> &cgroup_mounts,
                             const KernelApi *kernel)
    : kernel_(kernel) {
  // Create the mounted paths from the specified cgroup_mounts.
  set<string> mounted_paths;
  for (const auto &hierarchy_path_pair : cgroup_mounts) {
    bool owns_mount = true;

    // If this path has already been mounted, this hierarchy won't own the
    // mount.
    if (mounted_paths.find(hierarchy_path_pair.second) != mounted_paths.end()) {
      owns_mount = false;
    } else {
      mounted_paths.insert(hierarchy_path_pair.second);
    }

    mount_paths_.insert(
        make_pair(hierarchy_path_pair.first,
                  MountPoint(hierarchy_path_pair.second, owns_mount)));
  }
}

StatusOr<string> CgroupFactory::Get(CgroupHierarchy type,
                                    const string &hierarchy_path) const {
  // Get the cgroup path.
  StatusOr<string> statusor = GetCgroupPath(type, hierarchy_path);
  if (!statusor.ok()) {
    return statusor.status();
  }
  const string cgroup_path = statusor.ValueOrDie();

  // Ensure the cgroup already exists.
  if (kernel_->Access(cgroup_path, F_OK) != 0) {
    return Status(::util::error::NOT_FOUND,
                  Substitute("Expected cgroup \"$0\" to exist.", cgroup_path));
  }

  return cgroup_path;
}

StatusOr<string> CgroupFactory::Create(CgroupHierarchy type,
                                       const string &hierarchy_path) const {
  // Creating a controller that does not own the cgroup mount is like a Get().
  if (!OwnsCgroup(type)) {
    return Get(type, hierarchy_path);
  }

  // Get the cgroup path.
  StatusOr<string> statusor = GetCgroupPath(type, hierarchy_path);
  if (!statusor.ok()) {
    return statusor.status();
  }
  const string cgroup_path = statusor.ValueOrDie();

  // Ensure the cgroup does not already exists.
  if (kernel_->Access(cgroup_path, F_OK) == 0) {
    return Status(
        ::util::error::ALREADY_EXISTS,
        Substitute("Expected cgroup \"$0\" to not exist.", cgroup_path));
  }

  // Make the actual cgroup if we own the cgroup.
  if (kernel_->MkDir(cgroup_path) != 0) {
    return Status(::util::error::FAILED_PRECONDITION,
                  Substitute("Failed to create cgroup \"$0\".", cgroup_path));
  }

  return cgroup_path;
}

bool CgroupFactory::OwnsCgroup(CgroupHierarchy type) const {
  auto it = mount_paths_.find(type);
  if (it == mount_paths_.end()) {
    return false;
  }

  return it->second.owns;
}

// TODO(vmarmol): Allow the use of remount. This will faill if the hierarchies
// are already in use (with subcontainers), but it will gain us more flexibility
// at initialization time.
Status CgroupFactory::Mount(const CgroupMount &cgroup) {
  // Get number of existing mounts in the specified mount point.
  int existing_mounts_missing = 0;
  for (const auto hierarchy_mount_pair : mount_paths_) {
    if (cgroup.mount_path() == hierarchy_mount_pair.second.path) {
      existing_mounts_missing++;
    }
  }

  // Check if any of the hierarchies are already mounted elsewhere. We want to
  // make sure that we either have not mounted any hierarchies at the mount
  // point, or if we have that they are exactly the hierarchies the user
  // specified.
  vector<string> to_mount;
  for (int i = 0; i < cgroup.hierarchy_size(); ++i) {
    CgroupHierarchy hierarchy = cgroup.hierarchy(i);

    auto mount_path_it = mount_paths_.find(hierarchy);
    if (mount_path_it == mount_paths_.end()) {
      // Save the name of the hierarchy that we need to mount.
      const string hierarchy_name = GetHierarchyName(hierarchy);
      if (hierarchy_name.empty()) {
        return Status(
            ::util::error::INVALID_ARGUMENT,
            Substitute("Failed to mount unsupported hierarchy with id \"$0\"",
                       hierarchy));
      }
      to_mount.push_back(hierarchy_name);
    } else {
      // Ensure those already mounted are not mounted elsewhere.
      if (mount_path_it->second.path != cgroup.mount_path()) {
        return Status(
            ::util::error::INVALID_ARGUMENT,
            Substitute(
                "Hierarchy with ID \"$0\" is already mounted at \"$1\", "
                "can't mount again at \"$2\"",
                hierarchy, mount_path_it->second.path, cgroup.mount_path()));
      }

      // An existing mount was specified, remove from missing count.
      existing_mounts_missing--;
    }
  }

  // Check if some of the hierarchies are already mounted. We consider this an
  // error because we don't want to mount on top of the existing mount.
  if (existing_mounts_missing != 0) {
    // Some already mounted.
    return Status(
        ::util::error::INVALID_ARGUMENT,
        Substitute("Non-specified hierarchies are already mounted at \"$0\"",
                   cgroup.mount_path()));
  }

  // If nothing to mount, we're done.
  if (to_mount.empty()) {
    return Status::OK;
  }

  // Create the directory where the hierarchies will be mounted.
  if (kernel_->MkDirRecursive(cgroup.mount_path()) != 0) {
    return Status(
        ::util::error::FAILED_PRECONDITION,
        Substitute("Failed to recursively create \"$0\"", cgroup.mount_path()));
  }

  // Mount the hierarchies.
  const string hierarchies = Join(to_mount, ",");
  if (kernel_->Mount(kCgroupMountType, cgroup.mount_path(), kCgroupMountType, 0,
                     hierarchies.c_str()) != 0) {
    return Status(
        ::util::error::FAILED_PRECONDITION,
        Substitute("Failed to mount hierarchy with ID \"$0\" at \"$1\"",
                   hierarchies, cgroup.mount_path()));
  }

  // Save the hierarchies we just mounted.
  bool owns_mount = true;
  for (int i = 0; i < cgroup.hierarchy_size(); ++i) {
    CgroupHierarchy hierarchy = cgroup.hierarchy(i);
    mount_paths_.insert(
        make_pair(hierarchy, MountPoint(cgroup.mount_path(), owns_mount)));
    owns_mount = false;
  }

  return Status::OK;
}

bool CgroupFactory::IsMounted(CgroupHierarchy type) const {
  return mount_paths_.find(type) != mount_paths_.end();
}

StatusOr<string> CgroupFactory::GetCgroupPath(
    CgroupHierarchy hierarchy, const string &hierarchy_path) const {
  auto mount_path_it = mount_paths_.find(hierarchy);
  if (mount_path_it == mount_paths_.end()) {
    return Status(
        ::util::error::NOT_FOUND,
        Substitute("Did not find cgroup hierarchy with ID \"$0\"", hierarchy));
  }

  return JoinPath(mount_path_it->second.path, hierarchy_path);
}

StatusOr<string> CgroupFactory::DetectCgroupPath(
    pid_t tid, CgroupHierarchy hierarchy) const {
  // Get the name of the subsystem (cgroup hierarchy).
  const string subsystem_name = GetHierarchyName(hierarchy);
  if (subsystem_name.empty()) {
    return Status(::util::error::NOT_FOUND,
                  Substitute("Failed to get name for hierarchy with ID \"$0\"",
                             hierarchy));
  }

  // Find path for the specified subsystem.
  for (const ProcCgroupData &cgroup : ProcCgroup(tid)) {
    // Check all co-mounted subsystems.
    auto it = ::std::find(cgroup.subsystems.begin(), cgroup.subsystems.end(),
                          subsystem_name);
    if (it != cgroup.subsystems.end()) {
      return cgroup.hierarchy_path;
    }
  }

  return ::util::Status(
      ::util::error::NOT_FOUND,
      Substitute("Could not detect the container for TID \"$0\"", tid));
}

string CgroupFactory::GetHierarchyName(
    CgroupHierarchy hierarchy) const {
  for (const auto &name_hierarchy_pair : *g_supported_hierarchies) {
    if (name_hierarchy_pair.second == hierarchy) {
      return name_hierarchy_pair.first;
    }
  }

  return "";
}

vector<CgroupHierarchy> CgroupFactory::GetSupportedHierarchies()
    const {
  // Get all enabled cgroup hierarchies.
  set<string> enabled_hierarchies;
  for (const ProcCgroupsData &data : ProcCgroups()) {
    if (data.enabled) {
      enabled_hierarchies.insert(data.hierarchy_name);
    }
  }

  // Add all the enabled hierarchies we know to the supported set.
  vector<CgroupHierarchy> supported;
  for (const auto &name_hierarchy_pair : *g_supported_hierarchies) {
    if (enabled_hierarchies.find(name_hierarchy_pair.first) !=
        enabled_hierarchies.end()) {
      supported.push_back(name_hierarchy_pair.second);
    }
  }
  return supported;
}

// Populate the machine spec with cgroup_mounts. This code will turn a map of:
// { CgroupHierarchy -> MountPath } into a map of:
// { MountPath -> List<CgroupHierarchy> } within the machine spec.
Status CgroupFactory::PopulateMachineSpec(MachineSpec *spec) const {
  for (const auto name_hierarchy_pair : mount_paths_) {
    bool found_mount = false;

    // Look through the current list of cgroup_mounts to find a path
    // that matches the mounted path.
    for (auto &mount : *spec->mutable_cgroup_mount()) {
      if (mount.mount_path() == name_hierarchy_pair.second.path) {
        mount.add_hierarchy(name_hierarchy_pair.first);
        found_mount = true;
        continue;
      }
    }

    // If no cgroup_mount was found to have this mounted path, create a
    // new cgroup_mount which does.
    if (!found_mount) {
      CgroupMount *mount = spec->add_cgroup_mount();
      mount->set_mount_path(name_hierarchy_pair.second.path);
      mount->add_hierarchy(name_hierarchy_pair.first);
    }
  }
  return Status::OK;
}

}  // namespace lmctfy
}  // namespace containers
