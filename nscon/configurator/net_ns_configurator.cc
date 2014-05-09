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
// NetNsConfigurator implementation
//

#include "nscon/configurator/net_ns_configurator.h"

#include "gflags/gflags.h"
#include "include/namespaces.pb.h"
#include "util/errors.h"
#include "strings/join.h"
#include "strings/numbers.h"
#include "strings/substitute.h"
#include "util/process/subprocess.h"
#include "util/task/status.h"
#include "util/task/statusor.h"

using ::std::unique_ptr;
using ::strings::Join;
using ::strings::Substitute;
using ::util::Status;
using ::util::StatusOr;

DEFINE_string(nscon_ovs_bin,
              "/usr/local/bin/ovs-vsctl",
              "Path of ovs-vsctl binary on the host. Must be specified when "
              "using OVS bridge in the spec.");

namespace containers {
namespace nscon {

static const char kIpCmd[] = "/sbin/ip";
static const char kBrCtl[] = "/sbin/brctl";
static const char kSysCtl[] = "/sbin/sysctl";

Status NetNsConfigurator::RunCommand(const vector<string> &argv,
                                     SubProcess *sp) const {
  sp->SetArgv(argv);
  sp->SetChannelAction(CHAN_STDOUT, ACTION_PIPE);
  sp->SetChannelAction(CHAN_STDERR, ACTION_PIPE);
  if (!sp->Start()) {
    return Status(::util::error::INTERNAL,
                  Substitute("Could not run command: '$0': $1", Join(argv, " "),
                             strerror(errno)));
  }
  string stdout_output, stderr_output;
  if (sp->Communicate(&stdout_output, &stderr_output) != 0) {
    return Status(::util::error::INTERNAL,
                  Substitute(
                      "Failed command: '$0', error: $1: $2, stdout: $3, "
                      "stderr: $4",
                      Join(argv, " "), sp->exit_code(), sp->error_text(),
                      stdout_output, stderr_output));
  }
  return Status::OK;
}

vector<string> NetNsConfigurator::GetEthBridgeAddInterfaceCommand(
    const string &outside, const string &bridge) const {
  return {string(kBrCtl), "addif", bridge, outside};
}

vector<string> NetNsConfigurator::GetOvsBridgeAddInterfaceCommand(
    const string &outside, const string &bridge) const {
  return {FLAGS_nscon_ovs_bin, "add-port", bridge, outside};
}

vector<string> NetNsConfigurator::GetBridgeAddInterfaceCommand(
    const string &outside, const Network_Bridge &bridge) const {
  if (bridge.has_type() && bridge.type() == Network::Bridge::OVS) {
    return GetOvsBridgeAddInterfaceCommand(outside, bridge.name());
  }
  return GetEthBridgeAddInterfaceCommand(outside, bridge.name());
}

vector<string> NetNsConfigurator::GetCreateVethPairCommand(
    const string &outside, const string &inside, pid_t pid) const {
  return {string(kIpCmd), "link", "add", "name", outside, "type", "veth",
          "peer", "name", inside, "netns", SimpleItoa(pid)};
}

vector<string> NetNsConfigurator::GetMoveNetworkInterfaceToNsCommand(
    const string &interface, pid_t pid) const {
  return {string(kIpCmd), "link", "set", interface, "netns", SimpleItoa(pid)};
}

vector<string> NetNsConfigurator::GetActivateInterfaceCommand(
    const string &interface) const {
  return {string(kIpCmd), "link", "set", interface, "up"};
}

vector<string> NetNsConfigurator::GetSetMtuCommand(const string &interface,
                                                   int32 mtu) const {
  return {string(kIpCmd), "link", "set", interface, "mtu", SimpleItoa(mtu)};
}

vector<vector<string>> NetNsConfigurator::GetConfigureNetworkInterfaceCommands(
    const string &interface, const Network_VirtualIp &virtual_ip) const {
  vector<vector<string>> commands;
  if (virtual_ip.has_netmask() && !virtual_ip.netmask().empty()) {
    commands.push_back(
        {string(kIpCmd), "addr", "add",
         Substitute("$0/$1", virtual_ip.ip(), virtual_ip.netmask()),
         "dev", interface});
  } else {
    commands.push_back(
        {string(kIpCmd), "addr", "add", virtual_ip.ip(), "dev", interface});
  }

  if (virtual_ip.has_gateway() && !virtual_ip.gateway().empty()) {
    commands.push_back({string(kIpCmd), "route", "add", "default", "via",
                        virtual_ip.gateway(), "dev", interface});
  }

  if (virtual_ip.has_mtu()) {
    commands.push_back(GetSetMtuCommand(interface, virtual_ip.mtu()));
  }

  if (virtual_ip.has_ip_forward()) {
    const string value = virtual_ip.ip_forward() == true ? "1" : "0";
    commands.push_back({
      string(kSysCtl), "-w", Substitute("net.ipv4.ip_forward=$0", value)});
  }
  return commands;
}

Status NetNsConfigurator::SanityCheckNetSpec(const Network &net_spec) const {
  if (net_spec.has_interface() && net_spec.has_connection()) {
    return Status(::util::error::INVALID_ARGUMENT, "Exactly one of 'interface' "
                  "and 'veth_pair' must be specified.");
  }

  if (net_spec.has_connection()) {
    const auto &connection = net_spec.connection();
    if (!connection.has_veth_pair()) {
      return Status(::util::error::INVALID_ARGUMENT,
                    "Connection requires veth pair to be specified.");
    }
    const Network_VethPair &veth_pair = connection.veth_pair();
    if (!veth_pair.has_outside() || veth_pair.outside().empty()) {
      return Status(::util::error::INVALID_ARGUMENT,
                    "'outside' must be specified.");
    }
    if (!veth_pair.has_inside() || veth_pair.inside().empty()) {
      return Status(::util::error::INVALID_ARGUMENT,
                    "'inside' must be specified.");
    }
    if (connection.has_bridge()) {
      const auto &bridge = connection.bridge();
      if (!bridge.has_name() || bridge.name().empty()) {
        return Status(::util::error::INVALID_ARGUMENT,
                      "When bridge is specified its name has to be specified "
                      "too.");
      }

      if (bridge.has_type() && bridge.type() == Network::Bridge::OVS
          && FLAGS_nscon_ovs_bin.empty()) {
            return Status(::util::error::INVALID_ARGUMENT,
                          "When OVS bridge is specified, the commandline flag "
                          "--nscon_ovs_bin must be specified.");
      }
    }
  }

  if (net_spec.has_interface() && net_spec.interface().empty()) {
    return Status(::util::error::INVALID_ARGUMENT,
                  "Interface name cannot be empty.");
  }

  if (net_spec.has_virtual_ip()) {
    const Network_VirtualIp &virtual_ip = net_spec.virtual_ip();
    if (!virtual_ip.has_ip() || virtual_ip.ip().empty()) {
      return Status(::util::error::INVALID_ARGUMENT,
                    "Virtual IP must be specified.");
    }
  }

  return Status::OK;
}

Status NetNsConfigurator::SetupOutsideNamespace(const NamespaceSpec &spec,
                                                pid_t init_pid) const {
  if (!spec.has_net()) {
    return Status::OK;  // nothing to do.
  }

  const Network &net_spec = spec.net();
  RETURN_IF_ERROR(SanityCheckNetSpec(net_spec));

  unique_ptr<SubProcess> sp(subprocess_factory_->Run());
  if (net_spec.has_connection()) {
    const auto &connection = net_spec.connection();
    // Create a new veth pair, and move one of the interfaces to the net ns.
    const Network_VethPair &veth_pair = connection.veth_pair();
    const vector<string> argv = GetCreateVethPairCommand(veth_pair.outside(),
                                                         veth_pair.inside(),
                                                         init_pid);
    RETURN_IF_ERROR(RunCommand(argv, sp.get()));
    if (connection.has_bridge()) {
      RETURN_IF_ERROR(RunCommand(
          GetBridgeAddInterfaceCommand(veth_pair.outside(),
                                       connection.bridge()),
          sp.get()));
    }

    if (net_spec.has_virtual_ip() && net_spec.virtual_ip().has_mtu()) {
      RETURN_IF_ERROR(RunCommand(
          GetSetMtuCommand(veth_pair.outside(), net_spec.virtual_ip().mtu()),
          sp.get()));
    }
    RETURN_IF_ERROR(RunCommand(
        GetActivateInterfaceCommand(veth_pair.outside()),
        sp.get()));
    return Status::OK;
  }

  if (net_spec.has_interface()) {
    // Move the interface to the netns.
    const vector<string> argv =
        GetMoveNetworkInterfaceToNsCommand(net_spec.interface(), init_pid);
    return RunCommand(argv, sp.get());
  }

  // Nothing to do if neither interface nor connection are specified.
  return Status::OK;
}

Status
NetNsConfigurator::SetupInsideNamespace(const NamespaceSpec &spec) const {
  if (!spec.has_net()) {
    return Status::OK;  // nothing to do.
  }

  const Network &net_spec = spec.net();
  RETURN_IF_ERROR(SanityCheckNetSpec(net_spec));

  unique_ptr<SubProcess> sp(subprocess_factory_->Run());
  // Activate the loopback interface.
  const vector<string> argv_loopback = GetActivateInterfaceCommand("lo");
  RETURN_IF_ERROR(RunCommand(argv_loopback, sp.get()));

  string interface;
  if (net_spec.has_interface()) {
    interface = net_spec.interface();
  } else if (net_spec.has_connection()) {
    interface = net_spec.connection().veth_pair().inside();
  } else {
    return Status::OK;  // no interface inside namespace to configure.
  }

  // Activate the network interface.
  const vector<string> argv_iface = GetActivateInterfaceCommand(interface);
  RETURN_IF_ERROR(RunCommand(argv_iface, sp.get()));

  if (!net_spec.has_virtual_ip()) {
    return Status::OK;  // no further configuration required.
  }

  // Configure the network interface.
  const vector<vector<string>> argv_configure =
      GetConfigureNetworkInterfaceCommands(interface, net_spec.virtual_ip());
  for (const auto &argv : argv_configure) {
    RETURN_IF_ERROR(RunCommand(argv, sp.get()));
  }
  return Status::OK;
}

}  // namespace nscon
}  // namespace containers
