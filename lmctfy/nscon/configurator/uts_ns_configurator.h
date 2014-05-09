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

#ifndef PRODUCTION_CONTAINERS_NSCON_CONFIGURATOR_UTS_NS_CONFIGURATOR_H_
#define PRODUCTION_CONTAINERS_NSCON_CONFIGURATOR_UTS_NS_CONFIGURATOR_H_

#include <sys/types.h>  // for pid_t
#include <unistd.h>     // for sethostname(2)

#include "base/macros.h"
#include "nscon/configurator/ns_configurator.h"

namespace containers {
namespace nscon {

class NamespaceSpec;

// UtsNsConfigurator
//
// This class implements system configuration specific to the UTS namespace.
//
class UtsNsConfigurator : public NsConfigurator {
 public:
  explicit UtsNsConfigurator(NsUtil *ns_util)
      : NsConfigurator(CLONE_NEWUTS, ns_util) {}

  ~UtsNsConfigurator() override {}

  // This function implements the configuration to be performed *after*
  // switching to the UTS namespace.  It performs few sanity checks on |spec|,
  // and calls sethostname(2) to set the virtual hostname.
  // Returns status of the operation, OK iff successful.
  ::util::Status SetupInsideNamespace(const NamespaceSpec &spec) const override;

 private:
  friend class UtsNsConfiguratorTest;
  DISALLOW_COPY_AND_ASSIGN(UtsNsConfigurator);
};

}  // namespace nscon
}  // namespace containers

#endif  // PRODUCTION_CONTAINERS_NSCON_CONFIGURATOR_UTS_NS_CONFIGURATOR_H_
