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

#ifndef PRODUCTION_CONTAINERS_NSCON_CONFIGURATOR_FILESYSTEM_CONFIGURATOR_H_
#define PRODUCTION_CONTAINERS_NSCON_CONFIGURATOR_FILESYSTEM_CONFIGURATOR_H_

#include <set>
#include <string>
using ::std::string;

#include "base/macros.h"
#include "nscon/configurator/ns_configurator.h"
#include "strings/stringpiece.h"

namespace containers {
class Mounts;

namespace nscon {
class NamespaceSpec;

// FilesystemConfigurator
//
// This class implements configuration for FilesystemSpec. This is expected to
// be run only once per container.
//
// Class is thread-safe.
class FilesystemConfigurator : public NsConfigurator {
 public:
  // Use '0' for the clone-flag for this configurator.
  // TODO(adityakali): Since we are overloading 'NsConfigurator' to do a
  // non-namespace setup, consider renaming NsConfigurator to something else
  // (specially if there are going to be more of such non-namespace
  // implementations).
  explicit FilesystemConfigurator(NsUtil *ns_util)
      : NsConfigurator(0 /* ns */, ns_util) {}

  ~FilesystemConfigurator() override {}

  // Sets up the FilesystemSpec.
  ::util::Status SetupInsideNamespace(const NamespaceSpec &spec) const override;

 protected:
  ::util::Status PrepareFilesystem(const ::std::set<string> &whitelisted_mounts,
                                   const string &rootfs_path) const;
  ::util::Status SetupChroot(const string &rootfs_path) const;
  // Returns a list of mountpoints inside the namespace that must not be
  // unmounted.
  ::util::StatusOr<::std::set<string>> SetupExternalMounts(
       const Mounts &mounts,
       const string &rootfs_path) const;
  ::util::Status SetupPivotRoot(const string &rootfs_path) const;
  ::util::Status SetupProcfs(const string &procfs_path) const;
  ::util::Status SetupSysfs(const string &sysfs_path) const;
  ::util::Status SetupDevpts() const;

  static const char *kFsRoot;
  static const char *kDefaultProcfsPath;
  static const char *kDefaultSysfsPath;
  static const int kDefaultMountFlags;
  static const char *kDevptsMountData;
  static const char *kDefaultDevptsPath;
  static const char *kDevptmxPath;

 private:
  friend class FilesystemConfiguratorTest;
  DISALLOW_COPY_AND_ASSIGN(FilesystemConfigurator);
};

}  // namespace nscon
}  // namespace containers

#endif  // PRODUCTION_CONTAINERS_NSCON_CONFIGURATOR_FILESYSTEM_CONFIGURATOR_H_
