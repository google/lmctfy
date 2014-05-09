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

#ifndef PRODUCTION_CONTAINERS_NSCON_CONFIGURATOR_NET_NS_CONFIGURATOR_H_
#define PRODUCTION_CONTAINERS_NSCON_CONFIGURATOR_NET_NS_CONFIGURATOR_H_

#include <memory>

#include "base/callback.h"
#include "base/macros.h"
#include "nscon/configurator/ns_configurator.h"
#include "include/namespaces.pb.h"

class SubProcess;

namespace containers {
namespace nscon {
typedef ResultCallback<SubProcess *> SubProcessFactory;

class NetNsConfigurator : public NsConfigurator {
 public:
  // Takes ownership of |spf|.  Does not take ownership of |ns_util|.
  // TODO(ameyd): Remove 'ns' from the arguments of constructor.
  NetNsConfigurator(NsUtil *ns_util, SubProcessFactory *spf)
      : NsConfigurator(CLONE_NEWNET, ns_util), subprocess_factory_(spf) {}

  ~NetNsConfigurator() override {}

  // Perform the network namespace setup in the default namespace.
  // It simply assigns a network interface, if specified, to the namespace.
  ::util::Status SetupOutsideNamespace(const NamespaceSpec &spec,
                                       pid_t init_pid) const override;

  // Perform the network namespace setup in the network namespace.
  // It performs the following actions:
  // - activates the loopback interface in namespace,
  // - activates and configures the network interface if specified,
  // - connects the namespace to a gateway if specified.
  ::util::Status SetupInsideNamespace(const NamespaceSpec &spec) const override;

 private:
  // Helper function to sanity-check |net_spec|.
  // Returns OK iff the net_spec is correctly formed.
  ::util::Status SanityCheckNetSpec(const Network &net_spec) const;

  // Returns a command to connect the |outside| end of veth pair to the
  // specified |bridge|
  ::std::vector<string> GetBridgeAddInterfaceCommand(
      const string &outside, const Network_Bridge& bridge) const;

  // Returns a command to connect the |outside| end of veth pair to the
  // specified ethernet |bridge|.
  ::std::vector<string> GetEthBridgeAddInterfaceCommand(
      const string &outside, const string &bridge) const;

  // Returns a command to connect the |outside| end of veth pair to the
  // specified OpenVSwitch (OVS) |bridge|.
  ::std::vector<string> GetOvsBridgeAddInterfaceCommand(
      const string &outside, const string &bridge) const;

  // Returns a command that creates a veth pair, whose endpoints are names
  // |outside| and |inside|.  |outside| resides in the default namespace,
  // whereas |inside| is assigned to the network namespace.
  ::std::vector<string> GetCreateVethPairCommand(const string &outside,
                                                 const string &inside,
                                                 pid_t pid) const;

  // Returns a command that assigns a network interface to the network
  // namespace. |interface| is the human-readable label of the interface,
  // for example, eth0.
  ::std::vector<string> GetMoveNetworkInterfaceToNsCommand(
      const string &interface, pid_t pid) const;

  // Returns a command to activate the specified network |interface|.
  ::std::vector<string> GetActivateInterfaceCommand(
      const string &interface) const;

  // Returns a command to set |mtu| for a |interface|.
  ::std::vector<string> GetSetMtuCommand(const string &interface,
                                         int32 mtu) const;
  // Returns a command that brings up network |interface| inside the namespace,
  // assigns a virtual IP along with netmask (if specified) to it, and
  // adds a default route via gateway.  Both netmask and gateway are optional,
  // in which case the command will simply assign the virtual IP to the
  // interface.
  ::std::vector<::std::vector<string>> GetConfigureNetworkInterfaceCommands(
      const string &interface, const Network_VirtualIp &virtual_ip) const;

  ::util::Status RunCommand(const ::std::vector<string> &command,
                            SubProcess *sp) const;

  ::std::unique_ptr<SubProcessFactory> subprocess_factory_;

  friend class NetNsConfiguratorTest;
  DISALLOW_COPY_AND_ASSIGN(NetNsConfigurator);
};

}  // namespace nscon
}  // namespace containers

#endif  // PRODUCTION_CONTAINERS_NSCON_CONFIGURATOR_NET_NS_CONFIGURATOR_H_
