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

#ifndef PRODUCTION_CONTAINERS_NSCON_CONFIGURATOR_MNT_NS_CONFIGURATOR_H_
#define PRODUCTION_CONTAINERS_NSCON_CONFIGURATOR_MNT_NS_CONFIGURATOR_H_

#include <sys/types.h>  // for pid_t

#include "base/macros.h"
#include "nscon/configurator/ns_configurator.h"

namespace containers {
namespace nscon {

class NamespaceSpec;
class MntNsSpec_MountAction_Unmount;
class MntNsSpec_MountAction_Mount;

// MntNsConfigurator
//
// This class implements system configuration specific to the MNT namespace.
//
class MntNsConfigurator : public NsConfigurator {
 public:
  typedef MntNsSpec_MountAction_Unmount Unmount;
  typedef MntNsSpec_MountAction_Mount Mount;

  explicit MntNsConfigurator(NsUtil* ns_util)
      : NsConfigurator(CLONE_NEWNS, ns_util) {}

  ~MntNsConfigurator() override {}

  ::util::Status SetupInsideNamespace(const NamespaceSpec &spec) const override;

 private:
  ::util::Status DoUnmountAction(const Unmount &um) const;
  ::util::Status DoMountAction(const Mount &m) const;

  friend class MntNsConfiguratorTest;
  DISALLOW_COPY_AND_ASSIGN(MntNsConfigurator);
};

}  // namespace nscon
}  // namespace containers

#endif  // PRODUCTION_CONTAINERS_NSCON_CONFIGURATOR_MNT_NS_CONFIGURATOR_H_
