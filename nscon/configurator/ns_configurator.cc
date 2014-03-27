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
// Implements non-virtual members of NsConfigurator
//
#include "nscon/configurator/ns_configurator.h"

#include <fcntl.h>  // for O_RDONLY
#include <vector>

#include "file/base/path.h"
#include "nscon/ns_util.h"
#include "include/namespaces.pb.h"
#include "util/errors.h"
#include "util/scoped_cleanup.h"
#include "system_api/libc_fs_api.h"
#include "system_api/libc_process_api.h"
#include "strings/substitute.h"

using ::system_api::GlobalLibcFsApi;
using ::system_api::GlobalLibcProcessApi;
using ::system_api::ScopedFileCloser;
using ::std::vector;
using ::strings::Substitute;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace nscon {

Status NsConfigurator::SetupOutsideNamespace(const NamespaceSpec &spec,
                                             pid_t init_pid) const {
  return Status::OK;
}

Status
NsConfigurator::SetupInsideNamespace(const NamespaceSpec &spec) const {
  return Status::OK;
}

}  // namespace nscon
}  // namespace containers
