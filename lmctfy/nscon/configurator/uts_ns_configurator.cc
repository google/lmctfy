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
// UtsNsConfigurator implementation.
//
#include "nscon/configurator/uts_ns_configurator.h"

#include <limits.h>  // for HOST_NAME_MAX
#include <vector>

#include "include/namespaces.pb.h"
#include "util/errors.h"
#include "system_api/libc_net_api.h"
#include "strings/substitute.h"
#include "util/task/status.h"

using ::system_api::GlobalLibcNetApi;
using ::std::vector;
using ::strings::Substitute;
using ::util::Status;

namespace containers {
namespace nscon {

Status
UtsNsConfigurator::SetupInsideNamespace(const NamespaceSpec &spec) const {
  if (!spec.has_uts()) {
    return Status::OK;
  }

  const UtsNsSpec &uts_spec = spec.uts();
  if (uts_spec.has_vhostname()) {
    const string &hostname = uts_spec.vhostname();
    if (GlobalLibcNetApi()->SetHostname(
        hostname.c_str(), hostname.size()) < 0) {
      return Status(
          ::util::error::INTERNAL,
          Substitute("sethostname($0): $1", hostname, strerror(errno)));
    }
  }

  return Status::OK;
}

}  // namespace nscon
}  // namespace containers
