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

#ifndef PRODUCTION_CONTAINERS_NSCON_CONFIGURATOR_NS_CONFIGURATOR_H_
#define PRODUCTION_CONTAINERS_NSCON_CONFIGURATOR_NS_CONFIGURATOR_H_

#include <sys/types.h>  // for pid_t
#include <memory>

#include "base/macros.h"
#include "util/task/status.h"
#include "util/task/statusor.h"

namespace containers {
namespace nscon {

class NsUtil;
class NamespaceSpec;

//
// NsConfigurator
//
// This class provides an interface for namespace-specific implementation of
// configuration, and ensures the configuration of that namespace is
// performed in correct sequence.
//
// This class also provides a "do-nothing" implementation.  The namespaces
// that do not need to perform any configuration (for example, pid ns) can use
// the default implementation.
//
// This class is thread-hostile, because it modifies process-wide state
// and the namespace configuration and may alter state of the system
// (for example, create/remove mount points).
//
class NsConfigurator {
 public:
  // Constructor
  // Arguments:
  //   ns: a CLONE flag as expected by clone(2) syscall that identifies this
  //       configurator
  // Does NOT take ownership of |ns_util|.
  NsConfigurator(int ns, NsUtil *ns_util) : ns_(ns), ns_util_(ns_util) {}
  virtual ~NsConfigurator() {}

  // This function implements the configuration to be performed from outside
  // the namespace.
  // Arguments:
  //   spec: NamespaceSpec to be applied inside the namespace. Each namespace
  //         configurator will only apply its own spec if present.
  //   init_pid: Pid of the init process identifying the namespace to configure.
  // Returns status of the operation, OK iff successful.
  virtual ::util::Status SetupOutsideNamespace(const NamespaceSpec &spec,
                                               pid_t init_pid) const;

  // This function implements the configuration to be performed from inside
  // the namespace.
  // Arguments:
  //   spec: NamespaceSpec to be applied inside the namespace. Each namespace
  //         configurator will only apply its own spec if present.
  // Returns status of the operation, OK iff successful.
  virtual ::util::Status SetupInsideNamespace(const NamespaceSpec &spec) const;

  // Accessor
  int ns() const { return ns_; }

 protected:
  const int ns_;
  NsUtil *ns_util_;

 private:
  friend class NsConfiguratorTest;
  DISALLOW_COPY_AND_ASSIGN(NsConfigurator);
};

}  // namespace nscon
}  // namespace containers

#endif  // PRODUCTION_CONTAINERS_NSCON_CONFIGURATOR_NS_CONFIGURATOR_H_
