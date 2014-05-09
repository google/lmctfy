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

#ifndef PRODUCTION_CONTAINERS_NSCON_CONFIGURATOR_MACHINE_CONFIGURATOR_H_
#define PRODUCTION_CONTAINERS_NSCON_CONFIGURATOR_MACHINE_CONFIGURATOR_H_

#include "base/macros.h"

#include "include/config.pb.h"
#include "nscon/configurator/ns_configurator.h"

namespace containers {
namespace nscon {

class NamespaceSpec;

// MachineConfigurator
//
// Configures machine spec.
//
class MachineConfigurator : public NsConfigurator {
 public:
  explicit MachineConfigurator(NsUtil *ns_util)
      : NsConfigurator(0 /* ns */, ns_util) {}

  ~MachineConfigurator() override {}

  ::util::Status SetupInsideNamespace(const NamespaceSpec &spec) const override;

 protected:
  ::util::Status SetupRunTmpfs() const;
  ::util::Status WriteMachineSpec(const MachineSpec &spec,
                                  const string &directory,
                                  const string &filename) const;

 private:
  friend class MachineConfiguratorTest;
  DISALLOW_COPY_AND_ASSIGN(MachineConfigurator);
};

}  // namespace nscon
}  // namespace containers

#endif  // PRODUCTION_CONTAINERS_NSCON_CONFIGURATOR_MACHINE_CONFIGURATOR_H_
