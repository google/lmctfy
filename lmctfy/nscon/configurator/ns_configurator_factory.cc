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
// NsConfiguratorFactory implementation
//
#include "nscon/configurator/ns_configurator_factory.h"

#include "nscon/configurator/filesystem_configurator.h"
#include "nscon/configurator/machine_configurator.h"
#include "nscon/configurator/mnt_ns_configurator.h"
#include "nscon/configurator/net_ns_configurator.h"
#include "nscon/configurator/ns_configurator.h"
#include "nscon/configurator/user_ns_configurator.h"
#include "nscon/configurator/uts_ns_configurator.h"
#include "nscon/ns_util.h"
#include "util/errors.h"
#include "strings/substitute.h"
#include "util/process/subprocess.h"

using ::strings::Substitute;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace nscon {

StatusOr<NsConfiguratorFactory *> NsConfiguratorFactory::New(NsUtil *ns_util) {
  if (nullptr == ns_util) {
    return Status(::util::error::INVALID_ARGUMENT, "ns_util is null pointer");
  }
  return new NsConfiguratorFactory(ns_util);
}

static SubProcess *NewSubProcess() { return new SubProcess(); }

StatusOr<NsConfigurator *> NsConfiguratorFactory::Get(int ns) const {
  const char *ns_name = RETURN_IF_ERROR(ns_util_->NsCloneFlagToName(ns));

  switch (ns) {
    case CLONE_NEWUTS:
      return new UtsNsConfigurator(ns_util_);
    case CLONE_NEWPID:
      return new NsConfigurator(ns, ns_util_);  // Default implementation.
    case CLONE_NEWIPC:
      return new NsConfigurator(ns, ns_util_);  // Default implementation.
    case CLONE_NEWNET:
      return new NetNsConfigurator(ns_util_,
                                   NewPermanentCallback(&NewSubProcess));
    case CLONE_NEWNS:
      return new MntNsConfigurator(ns_util_);
    case CLONE_NEWUSER:
      return new UserNsConfigurator(ns_util_);
    default:
      return Status(
          ::util::error::NOT_FOUND,
          Substitute("Configurator not found for namespace: $0", ns_name));
  }
}

StatusOr<NsConfigurator *>
NsConfiguratorFactory::GetFilesystemConfigurator() const {
  return new FilesystemConfigurator(ns_util_);
}

StatusOr<NsConfigurator *>
NsConfiguratorFactory::GetMachineConfigurator() const {
  return new MachineConfigurator(ns_util_);
}

}  // namespace nscon
}  // namespace containers
