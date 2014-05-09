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
// Unit tests for NetNsConfigurator and ShellCommandRunner classes
//

#include "nscon/configurator/net_ns_configurator.h"

#include "nscon/ns_util_mock.h"
#include "include/namespaces.pb.h"
#include "util/errors_test_util.h"
#include "strings/join.h"
#include "gtest/gtest.h"
#include "util/process/mock_subprocess.h"
#include "util/task/status.h"
#include "util/task/statusor.h"

using ::std::unique_ptr;
using ::strings::Join;
using ::testing::_;
using ::testing::AtLeast;
using ::testing::Sequence;
using ::testing::NotNull;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::StrEq;
using ::util::Status;
using ::util::StatusOr;

DECLARE_string(nscon_ovs_bin);

namespace containers {
namespace nscon {

const pid_t kPid = 9999;
const char kBin[] = "/bin/true";
const char kInterface[] = "veth1";
const char kVip[] = "10.0.0.1";
const char kNetmask[] = "255.255.255.0";
const char kGateway[] = "10.0.0.254";

// Returns the specified subprocess.
SubProcess *IdentitySubProcessFactory(MockSubProcess *subprocess) {
  return subprocess;
}

class NetNsConfiguratorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    mock_ns_util_.reset(new ::testing::StrictMock<MockNsUtil>());
    mock_subprocess_ = new ::testing::StrictMock<MockSubProcess>();
    SubProcessFactory *mock_subprocess_factory =
        NewPermanentCallback(&IdentitySubProcessFactory, mock_subprocess_);
    net_ns_config_.reset(new NetNsConfigurator(mock_ns_util_.get(),
                                               mock_subprocess_factory));
  }

  Status CallRunCommand(const vector<string> &argv, SubProcess *sp) {
    return net_ns_config_->RunCommand(argv, sp);
  }

  Status CallSanityCheckNetSpec(const Network &net_spec) {
    return net_ns_config_->SanityCheckNetSpec(net_spec);
  }

  vector<string> CallGetEthBridgeAddInterfaceCommand(
      const string &outside, const string &bridge) const {
    return net_ns_config_->GetEthBridgeAddInterfaceCommand(outside, bridge);
  }

  vector<string> CallGetOvsBridgeAddInterfaceCommand(
      const string &outside, const string &bridge) const {
    return net_ns_config_->GetOvsBridgeAddInterfaceCommand(outside, bridge);
  }

  vector<string> CallGetBridgeAddInterfaceCommand(
      const string &outside, const Network_Bridge &bridge) {
    return net_ns_config_->GetBridgeAddInterfaceCommand(outside, bridge);
  }

  vector<string> CallCreateVethPairCommand(const string &outside,
                                           const string &inside,
                                           pid_t pid) const {
    return net_ns_config_->GetCreateVethPairCommand(outside, inside, pid);
  }

  vector<string> CallMoveNetworkInterfaceToNsCommand(const string &interface,
                                                     pid_t pid) const {
    return net_ns_config_->GetMoveNetworkInterfaceToNsCommand(interface, pid);
  }

  vector<string> CallActivateInterfaceCommand(const string &interface) {
    return net_ns_config_->GetActivateInterfaceCommand(interface);
  }

  vector<vector<string>> CallConfigureNetworkInterfaceCommands(
      const string &interface, const Network_VirtualIp &virtual_ip) {
    return net_ns_config_->GetConfigureNetworkInterfaceCommands(interface,
                                                                virtual_ip);
  }

  void CommonMockSubProcessSetup() {
    EXPECT_CALL(*mock_subprocess_, SetChannelAction(_, _)).Times(AtLeast(2));
    EXPECT_CALL(*mock_subprocess_, Start()).Times(AtLeast(1))
        .WillRepeatedly(Return(true));
  }

  MockSubProcess *mock_subprocess_;

 protected:
  unique_ptr<MockNsUtil> mock_ns_util_;
  unique_ptr<NetNsConfigurator> net_ns_config_;
};

TEST_F(NetNsConfiguratorTest, RunCommand_StartFailure) {
  vector<string> argv = {kBin};
  EXPECT_CALL(*mock_subprocess_, SetArgv(argv));
  EXPECT_CALL(*mock_subprocess_, SetChannelAction(_, _)).Times(2);
  EXPECT_CALL(*mock_subprocess_, Start()).WillOnce(Return(false));

  EXPECT_ERROR_CODE(::util::error::INTERNAL,
                    CallRunCommand(argv, mock_subprocess_));
  delete mock_subprocess_;
}

TEST_F(NetNsConfiguratorTest, RunCommand_CmdFailure) {
  vector<string> argv = {kBin};
  EXPECT_CALL(*mock_subprocess_, SetArgv(argv));
  EXPECT_CALL(*mock_subprocess_, SetChannelAction(_, _)).Times(2);
  EXPECT_CALL(*mock_subprocess_, Start()).WillOnce(Return(true));
  EXPECT_CALL(*mock_subprocess_, Communicate(NotNull(), NotNull()))
      .WillOnce(Return(-1));
  EXPECT_CALL(*mock_subprocess_, exit_code()).WillOnce(Return(-1));
  EXPECT_CALL(*mock_subprocess_, error_text()).WillOnce(Return(""));

  EXPECT_ERROR_CODE(::util::error::INTERNAL,
                    CallRunCommand(argv, mock_subprocess_));
  delete mock_subprocess_;
}

TEST_F(NetNsConfiguratorTest, RunCommand_Success) {
  vector<string> argv = {kBin};
  EXPECT_CALL(*mock_subprocess_, SetArgv(argv));
  EXPECT_CALL(*mock_subprocess_, SetChannelAction(_, _)).Times(2);
  EXPECT_CALL(*mock_subprocess_, Start()).WillOnce(Return(true));
  EXPECT_CALL(*mock_subprocess_, Communicate(NotNull(), NotNull()))
      .WillOnce(Return(0));

  ASSERT_OK(CallRunCommand(argv, mock_subprocess_));
  delete mock_subprocess_;
}

TEST_F(NetNsConfiguratorTest, GetBridgeAddInterfaceCommand_Default) {
  Network_Bridge bridge;
  bridge.set_name("my_bridge");
  const vector<string> command =
      CallGetBridgeAddInterfaceCommand("foo_veth", bridge);
  EXPECT_EQ("/sbin/brctl addif my_bridge foo_veth", Join(command, " "));
}

TEST_F(NetNsConfiguratorTest, GetBridgeAddInterfaceCommand_EthType) {
  Network_Bridge bridge;
  bridge.set_name("my_bridge");
  bridge.set_type(Network::Bridge::ETH);
  const vector<string> command =
      CallGetBridgeAddInterfaceCommand("foo_veth", bridge);
  EXPECT_EQ("/sbin/brctl addif my_bridge foo_veth", Join(command, " "));
}

TEST_F(NetNsConfiguratorTest, GetBridgeAddInterfaceCommand_OvsType) {
  Network_Bridge bridge;
  bridge.set_name("my_bridge");
  bridge.set_type(Network::Bridge::OVS);
  FLAGS_nscon_ovs_bin = "ovs-vsctl";
  const vector<string> command =
      CallGetBridgeAddInterfaceCommand("foo_veth", bridge);
  EXPECT_EQ("ovs-vsctl add-port my_bridge foo_veth", Join(command, " "));
}

TEST_F(NetNsConfiguratorTest, GetCreateVethPairCommand) {
  const vector<string> command = CallCreateVethPairCommand("foo", "bar", kPid);
  EXPECT_EQ("/sbin/ip link add name foo type veth peer name bar netns 9999",
            Join(command, " "));
}

TEST_F(NetNsConfiguratorTest, MoveNetworkInterfaceToNsCommand) {
  const vector<string> command =
      CallMoveNetworkInterfaceToNsCommand(kInterface, kPid);
  EXPECT_EQ("/sbin/ip link set veth1 netns 9999", Join(command, " "));
}

TEST_F(NetNsConfiguratorTest, ActivateInterfaceCommand) {
  const vector<string> command = CallActivateInterfaceCommand("foo");
  EXPECT_EQ("/sbin/ip link set foo up", Join(command, " "));
}

TEST_F(NetNsConfiguratorTest, ConfigureNetworkInterfaceCommands_IpOnly) {
  vector<string> expected = {"/sbin/ip addr add 10.0.0.1 dev veth1"};
  Network_VirtualIp virtual_ip;
  virtual_ip.set_ip(kVip);
  const vector<vector<string>> commands =
      CallConfigureNetworkInterfaceCommands(kInterface, virtual_ip);
  EXPECT_EQ(commands.size(), expected.size());
  for (int i = 0; i < commands.size(); ++i) {
    EXPECT_EQ(expected[i], Join(commands[i], " "));
  }
}

TEST_F(NetNsConfiguratorTest, ConfigureNetworkInterfaceCommands_IpAndNetmask) {
  vector<string> expected = {
      "/sbin/ip addr add 10.0.0.1/255.255.255.0 dev veth1"};

  Network_VirtualIp virtual_ip;
  virtual_ip.set_ip(kVip);
  virtual_ip.set_netmask(kNetmask);
  const vector<vector<string>> commands =
      CallConfigureNetworkInterfaceCommands(kInterface, virtual_ip);
  EXPECT_EQ(commands.size(), expected.size());
  for (int i = 0; i < commands.size(); ++i) {
    EXPECT_EQ(expected[i], Join(commands[i], " "));
  }
}

TEST_F(NetNsConfiguratorTest, ConfigureNetworkInterfaceCommands) {
  vector<string> expected = {
      "/sbin/ip addr add 10.0.0.1 dev veth1",
      "/sbin/ip route add default via 10.0.0.254 dev veth1",
      "/sbin/ip link set veth1 mtu 1500",
      "/sbin/sysctl -w net.ipv4.ip_forward=1"};

  Network_VirtualIp virtual_ip;
  virtual_ip.set_ip(kVip);
  virtual_ip.set_netmask("");
  virtual_ip.set_gateway(kGateway);
  virtual_ip.set_mtu(1500);
  virtual_ip.set_ip_forward(true);
  const vector<vector<string>> commands =
      CallConfigureNetworkInterfaceCommands(kInterface, virtual_ip);
  EXPECT_EQ(commands.size(), expected.size());
  for (int i = 0; i < commands.size(); ++i) {
    EXPECT_EQ(expected[i], Join(commands[i], " "));
  }
}

TEST_F(NetNsConfiguratorTest,
       ConfigureNetworkInterfaceCommands_IpForwardFalse) {
  vector<string> expected = {
      "/sbin/ip addr add 10.0.0.1 dev veth1",
      "/sbin/sysctl -w net.ipv4.ip_forward=0"};

  Network_VirtualIp virtual_ip;
  virtual_ip.set_ip(kVip);
  virtual_ip.set_ip_forward(false);
  const vector<vector<string>> commands =
      CallConfigureNetworkInterfaceCommands(kInterface, virtual_ip);
  EXPECT_EQ(commands.size(), expected.size());
  for (int i = 0; i < commands.size(); ++i) {
    EXPECT_EQ(expected[i], Join(commands[i], " "));
  }
}

TEST_F(NetNsConfiguratorTest, SanityCheckNetSpec_InterfaceAndConnection) {
  Network net_spec;
  net_spec.mutable_interface();
  net_spec.mutable_connection();

  EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT,
                    CallSanityCheckNetSpec(net_spec));
}

TEST_F(NetNsConfiguratorTest, SanityCheckNetSpec_InterfaceAndVethPair) {
  Network net_spec;
  net_spec.mutable_interface();
  net_spec.mutable_connection()->mutable_veth_pair();

  EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT,
                    CallSanityCheckNetSpec(net_spec));
}

TEST_F(NetNsConfiguratorTest, SanityCheckNetSpec_EmptyInterface) {
  Network net_spec;
  net_spec.mutable_interface();

  EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT,
                    CallSanityCheckNetSpec(net_spec));
}

TEST_F(NetNsConfiguratorTest, SanityCheckNetSpec_InterfaceSuccess) {
  Network net_spec;
  net_spec.set_interface(kInterface);

  ASSERT_OK(CallSanityCheckNetSpec(net_spec));
}

TEST_F(NetNsConfiguratorTest, SanityCheckNetSpec_NoVethOut) {
  Network net_spec;
  net_spec.mutable_connection()->mutable_veth_pair()->mutable_inside();

  EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT,
                    CallSanityCheckNetSpec(net_spec));
}

TEST_F(NetNsConfiguratorTest, SanityCheckNetSpec_EmptyVethOut) {
  Network net_spec;
  net_spec.mutable_connection()->mutable_veth_pair()->mutable_outside();

  EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT,
                    CallSanityCheckNetSpec(net_spec));
}

TEST_F(NetNsConfiguratorTest, SanityCheckNetSpec_NoVethIn) {
  Network net_spec;
  net_spec.mutable_connection()->mutable_veth_pair()->set_outside(kInterface);

  EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT,
                    CallSanityCheckNetSpec(net_spec));
}

TEST_F(NetNsConfiguratorTest, SanityCheckNetSpec_EmptyVethIn) {
  Network net_spec;
  net_spec.mutable_connection()->mutable_veth_pair()->set_outside(kInterface);
  net_spec.mutable_connection()->mutable_veth_pair()->mutable_inside();

  EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT,
                    CallSanityCheckNetSpec(net_spec));
}

TEST_F(NetNsConfiguratorTest, SanityCheckNetSpec_VethSuccess) {
  Network net_spec;
  net_spec.mutable_connection()->mutable_veth_pair()->set_outside(kInterface);
  net_spec.mutable_connection()->mutable_veth_pair()->set_inside(kInterface);

  ASSERT_OK(CallSanityCheckNetSpec(net_spec));
}

TEST_F(NetNsConfiguratorTest, SanityCheckNetSpec_NoBridgeName) {
  Network net_spec;
  net_spec.mutable_connection()->mutable_veth_pair()->set_outside(kInterface);
  net_spec.mutable_connection()->mutable_veth_pair()->set_inside(kInterface);
  net_spec.mutable_connection()->mutable_bridge();

  ASSERT_ERROR_CODE(::util::error::INVALID_ARGUMENT,
                    CallSanityCheckNetSpec(net_spec));
}

TEST_F(NetNsConfiguratorTest, SanityCheckNetSpec_EmptyBridgeName) {
  Network net_spec;
  net_spec.mutable_connection()->mutable_veth_pair()->set_outside(kInterface);
  net_spec.mutable_connection()->mutable_veth_pair()->set_inside(kInterface);
  net_spec.mutable_connection()->mutable_bridge()->set_name("");

  ASSERT_ERROR_CODE(::util::error::INVALID_ARGUMENT,
                    CallSanityCheckNetSpec(net_spec));
}

TEST_F(NetNsConfiguratorTest, SanityCheckNetSpec_NoBridgeType) {
  Network net_spec;
  net_spec.mutable_connection()->mutable_veth_pair()->set_outside(kInterface);
  net_spec.mutable_connection()->mutable_veth_pair()->set_inside(kInterface);
  net_spec.mutable_connection()->mutable_bridge()->set_name("foo");

  ASSERT_OK(CallSanityCheckNetSpec(net_spec));
}

TEST_F(NetNsConfiguratorTest, SanityCheckNetSpec_EthBridgeSuccess) {
  Network net_spec;
  net_spec.mutable_connection()->mutable_veth_pair()->set_outside(kInterface);
  net_spec.mutable_connection()->mutable_veth_pair()->set_inside(kInterface);
  net_spec.mutable_connection()->mutable_bridge()->set_name("foo");
  net_spec.mutable_connection()->mutable_bridge()->set_type(
      Network::Bridge::ETH);

  ASSERT_OK(CallSanityCheckNetSpec(net_spec));
}

TEST_F(NetNsConfiguratorTest, SanityCheckNetSpec_OvsBridgeError) {
  Network net_spec;
  net_spec.mutable_connection()->mutable_veth_pair()->set_outside(kInterface);
  net_spec.mutable_connection()->mutable_veth_pair()->set_inside(kInterface);
  net_spec.mutable_connection()->mutable_bridge()->set_name("foo");
  net_spec.mutable_connection()->mutable_bridge()->set_type(
      Network::Bridge::OVS);
  FLAGS_nscon_ovs_bin = "";

  ASSERT_ERROR_CODE(::util::error::INVALID_ARGUMENT,
                    CallSanityCheckNetSpec(net_spec));
}

TEST_F(NetNsConfiguratorTest, SanityCheckNetSpec_OvsBridgeSuccess) {
  Network net_spec;
  net_spec.mutable_connection()->mutable_veth_pair()->set_outside(kInterface);
  net_spec.mutable_connection()->mutable_veth_pair()->set_inside(kInterface);
  net_spec.mutable_connection()->mutable_bridge()->set_name("foo");
  net_spec.mutable_connection()->mutable_bridge()->set_type(
      Network::Bridge::OVS);
  FLAGS_nscon_ovs_bin = "ovs-vsctl";

  ASSERT_OK(CallSanityCheckNetSpec(net_spec));
}

TEST_F(NetNsConfiguratorTest, SanityCheckNetSpec_NoIp) {
  Network net_spec;
  net_spec.mutable_virtual_ip();

  EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT,
                    CallSanityCheckNetSpec(net_spec));
}

TEST_F(NetNsConfiguratorTest, SanityCheckNetSpec_EmptyIp) {
  Network net_spec;
  net_spec.mutable_virtual_ip()->set_ip("");

  EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT,
                    CallSanityCheckNetSpec(net_spec));
}

TEST_F(NetNsConfiguratorTest, SanityCheckNetSpec_IpSuccess) {
  Network net_spec;
  net_spec.mutable_virtual_ip()->set_ip(kVip);

  ASSERT_OK(CallSanityCheckNetSpec(net_spec));
}

TEST_F(NetNsConfiguratorTest, SetupOutsideNamespace_NoSpec) {
  const NamespaceSpec spec;
  ASSERT_OK(net_ns_config_->SetupOutsideNamespace(spec, kPid));
}

TEST_F(NetNsConfiguratorTest, SetupOutsideNamespace_NoInterface) {
  NamespaceSpec spec;
  spec.mutable_net();

  ASSERT_OK(net_ns_config_->SetupOutsideNamespace(spec, kPid));
}

TEST_F(NetNsConfiguratorTest, SetupOutsideNamespace_SanityCheckFailure) {
  NamespaceSpec spec;
  spec.mutable_net()->set_interface("");

  EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT,
                    net_ns_config_->SetupOutsideNamespace(spec, kPid));
}

TEST_F(NetNsConfiguratorTest, SetupOutsideNamespace_CreateVethFails) {
  NamespaceSpec spec;
  auto *connection = spec.mutable_net()->mutable_connection();
  connection->mutable_veth_pair()->set_outside("vethXYZ123");
  connection->mutable_veth_pair()->set_inside(kInterface);

  const vector<string> command =
      CallCreateVethPairCommand("vethXYZ123", kInterface, kPid);
  CommonMockSubProcessSetup();
  EXPECT_CALL(*mock_subprocess_, SetArgv(command));
  EXPECT_CALL(*mock_subprocess_, Communicate(NotNull(), NotNull()))
      .WillOnce(Return(-1));
  EXPECT_CALL(*mock_subprocess_, exit_code()).WillOnce(Return(-1));
  EXPECT_CALL(*mock_subprocess_, error_text()).WillOnce(Return(""));

  EXPECT_ERROR_CODE(::util::error::INTERNAL,
                    net_ns_config_->SetupOutsideNamespace(spec, kPid));
}

TEST_F(NetNsConfiguratorTest, SetupOutsideNamespace_ActivateVethFails) {
  NamespaceSpec spec;
  auto *connection = spec.mutable_net()->mutable_connection();
  connection->mutable_veth_pair()->set_outside("vethXYZ123");
  connection->mutable_veth_pair()->set_inside(kInterface);

  const vector<string> command =
      CallCreateVethPairCommand("vethXYZ123", kInterface, kPid);
  CommonMockSubProcessSetup();
  Sequence s;
  EXPECT_CALL(*mock_subprocess_, SetArgv(command)).InSequence(s);
  EXPECT_CALL(*mock_subprocess_, Communicate(NotNull(), NotNull()))
      .InSequence(s)
      .WillOnce(Return(0))
      .RetiresOnSaturation();
  EXPECT_CALL(*mock_subprocess_,
              SetArgv(CallActivateInterfaceCommand("vethXYZ123")))
      .InSequence(s);
  ::testing::Expectation expectation_communicate =
      EXPECT_CALL(*mock_subprocess_, Communicate(NotNull(), NotNull()))
      .InSequence(s)
      .WillOnce(Return(-1))
      .RetiresOnSaturation();
  EXPECT_CALL(*mock_subprocess_, exit_code())
      .After(expectation_communicate)
      .WillOnce(Return(-1));
  EXPECT_CALL(*mock_subprocess_, error_text())
      .After(expectation_communicate)
      .WillOnce(Return(""));

  EXPECT_ERROR_CODE(::util::error::INTERNAL,
                    net_ns_config_->SetupOutsideNamespace(spec, kPid));
}

TEST_F(NetNsConfiguratorTest, SetupOutsideNamespace_CreateVethSuccess) {
  NamespaceSpec spec;
  auto *connection = spec.mutable_net()->mutable_connection();
  connection->mutable_veth_pair()->set_outside("vethXYZ123");
  connection->mutable_veth_pair()->set_inside(kInterface);

  const vector<string> command =
      CallCreateVethPairCommand("vethXYZ123", kInterface, kPid);
  CommonMockSubProcessSetup();
  EXPECT_CALL(*mock_subprocess_, SetArgv(command));
  EXPECT_CALL(*mock_subprocess_,
              SetArgv(CallActivateInterfaceCommand("vethXYZ123")));
  EXPECT_CALL(*mock_subprocess_, Communicate(NotNull(), NotNull()))
      .WillRepeatedly(Return(0));

  ASSERT_OK(net_ns_config_->SetupOutsideNamespace(spec, kPid));
}

TEST_F(NetNsConfiguratorTest, SetupOutsideNamespace_MoveDevToNsFails) {
  NamespaceSpec spec;
  spec.mutable_net()->set_interface(kInterface);

  const vector<string> command =
      CallMoveNetworkInterfaceToNsCommand(kInterface, kPid);
  CommonMockSubProcessSetup();
  EXPECT_CALL(*mock_subprocess_, SetArgv(command));
  EXPECT_CALL(*mock_subprocess_, Communicate(NotNull(), NotNull()))
      .WillOnce(Return(-1));
  EXPECT_CALL(*mock_subprocess_, exit_code()).WillOnce(Return(-1));
  EXPECT_CALL(*mock_subprocess_, error_text()).WillOnce(Return(""));

  EXPECT_ERROR_CODE(::util::error::INTERNAL,
                    net_ns_config_->SetupOutsideNamespace(spec, kPid));
}

TEST_F(NetNsConfiguratorTest, SetupOutsideNamespace_BridgeSetupFails) {
  NamespaceSpec spec;
  auto *connection = spec.mutable_net()->mutable_connection();
  connection->mutable_veth_pair()->set_outside("vethXYZ123");
  connection->mutable_veth_pair()->set_inside(kInterface);
  connection->mutable_bridge()->set_name("my_bridge");

  const vector<string> command =
      CallCreateVethPairCommand("vethXYZ123", kInterface, kPid);
  CommonMockSubProcessSetup();
  EXPECT_CALL(*mock_subprocess_, SetArgv(command));
  EXPECT_CALL(*mock_subprocess_,
              SetArgv(CallGetEthBridgeAddInterfaceCommand("vethXYZ123",
                                                          "my_bridge")));
  EXPECT_CALL(*mock_subprocess_, Communicate(NotNull(), NotNull()))
      .WillOnce(Return(-1))
      .RetiresOnSaturation();
  EXPECT_CALL(*mock_subprocess_, Communicate(NotNull(), NotNull()))
      .WillOnce(Return(0))
      .RetiresOnSaturation();
  EXPECT_CALL(*mock_subprocess_, exit_code()).WillOnce(Return(-1));
  EXPECT_CALL(*mock_subprocess_, error_text()).WillOnce(Return(""));

  EXPECT_ERROR_CODE(::util::error::INTERNAL,
                    net_ns_config_->SetupOutsideNamespace(spec, kPid));
}

TEST_F(NetNsConfiguratorTest, SetupOutsideNamespace_BridgeSetupSucceeds) {
  NamespaceSpec spec;
  auto *connection = spec.mutable_net()->mutable_connection();
  connection->mutable_veth_pair()->set_outside("vethXYZ123");
  connection->mutable_veth_pair()->set_inside(kInterface);
  connection->mutable_bridge()->set_name("my_bridge");

  const vector<string> command =
      CallCreateVethPairCommand("vethXYZ123", kInterface, kPid);
  CommonMockSubProcessSetup();
  EXPECT_CALL(*mock_subprocess_, SetArgv(command));
  EXPECT_CALL(*mock_subprocess_,
              SetArgv(CallGetEthBridgeAddInterfaceCommand("vethXYZ123",
                                                          "my_bridge")));
  EXPECT_CALL(*mock_subprocess_,
              SetArgv(CallActivateInterfaceCommand("vethXYZ123")));
  EXPECT_CALL(*mock_subprocess_, Communicate(NotNull(), NotNull()))
      .WillRepeatedly(Return(0));

  ASSERT_OK(net_ns_config_->SetupOutsideNamespace(spec, kPid));
}

TEST_F(NetNsConfiguratorTest, SetupOutsideNamespace_OvsBridgeSetupSucceeds) {
  NamespaceSpec spec;
  auto *connection = spec.mutable_net()->mutable_connection();
  connection->mutable_veth_pair()->set_outside("vethXYZ123");
  connection->mutable_veth_pair()->set_inside(kInterface);
  connection->mutable_bridge()->set_name("my_bridge");
  connection->mutable_bridge()->set_type(Network::Bridge::OVS);
  FLAGS_nscon_ovs_bin = "ovs-vsctl";

  const vector<string> command =
      CallCreateVethPairCommand("vethXYZ123", kInterface, kPid);
  CommonMockSubProcessSetup();
  EXPECT_CALL(*mock_subprocess_, SetArgv(command));
  EXPECT_CALL(*mock_subprocess_,
              SetArgv(CallGetOvsBridgeAddInterfaceCommand("vethXYZ123",
                                                          "my_bridge")));
  EXPECT_CALL(*mock_subprocess_,
              SetArgv(CallActivateInterfaceCommand("vethXYZ123")));
  EXPECT_CALL(*mock_subprocess_, Communicate(NotNull(), NotNull()))
      .WillRepeatedly(Return(0));

  ASSERT_OK(net_ns_config_->SetupOutsideNamespace(spec, kPid));
}

TEST_F(NetNsConfiguratorTest, SetupOutsideNamespace_MoveDevToNsSuccess) {
  NamespaceSpec spec;
  spec.mutable_net()->set_interface(kInterface);

  const vector<string> command =
      CallMoveNetworkInterfaceToNsCommand(kInterface, kPid);
  CommonMockSubProcessSetup();
  EXPECT_CALL(*mock_subprocess_, SetArgv(command));
  EXPECT_CALL(*mock_subprocess_, Communicate(NotNull(), NotNull()))
      .WillOnce(Return(0));

  ASSERT_OK(net_ns_config_->SetupOutsideNamespace(spec, kPid));
}

TEST_F(NetNsConfiguratorTest, SetupInsideNamespace_NoSpec) {
  const NamespaceSpec spec;
  ASSERT_OK(net_ns_config_->SetupInsideNamespace(spec));
}

TEST_F(NetNsConfiguratorTest, SetupInsideNamespace_SanityCheckFailure) {
  NamespaceSpec spec;
  spec.mutable_net()->set_interface("");

  EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT,
                    net_ns_config_->SetupInsideNamespace(spec));
}

TEST_F(NetNsConfiguratorTest, SetupInsideNamespace_ActivateLoopbackFails) {
  NamespaceSpec spec;
  spec.mutable_net();

  const vector<string> command = CallActivateInterfaceCommand("lo");
  CommonMockSubProcessSetup();
  EXPECT_CALL(*mock_subprocess_, SetArgv(command));
  EXPECT_CALL(*mock_subprocess_, Communicate(NotNull(), NotNull()))
      .WillOnce(Return(-1));
  EXPECT_CALL(*mock_subprocess_, exit_code()).WillOnce(Return(-1));
  EXPECT_CALL(*mock_subprocess_, error_text()).WillOnce(Return(""));

  EXPECT_ERROR_CODE(::util::error::INTERNAL,
                    net_ns_config_->SetupInsideNamespace(spec));
}

TEST_F(NetNsConfiguratorTest, SetupInsideNamespace_ConfigureInterfaceFails) {
  NamespaceSpec spec;
  spec.mutable_net()->set_interface(kInterface);
  spec.mutable_net()->mutable_virtual_ip()->set_ip(kVip);
  spec.mutable_net()->mutable_virtual_ip()->set_netmask(kNetmask);
  spec.mutable_net()->mutable_virtual_ip()->set_gateway(kGateway);

  const vector<string> command = CallActivateInterfaceCommand("lo");
  EXPECT_CALL(*mock_subprocess_, SetArgv(command));

  const vector<string> command2 = CallActivateInterfaceCommand(kInterface);
  EXPECT_CALL(*mock_subprocess_, SetArgv(command2));

  const vector<vector<string>> commands = CallConfigureNetworkInterfaceCommands(
      kInterface, spec.net().virtual_ip());
  for (const auto &argv : commands) {
    EXPECT_CALL(*mock_subprocess_, SetArgv(argv));
  }

  CommonMockSubProcessSetup();
  EXPECT_CALL(*mock_subprocess_, Communicate(NotNull(), NotNull()))
      .WillOnce(Return(0))
      .WillOnce(Return(0))
      .WillOnce(Return(0))
      .WillOnce(Return(-1));
  EXPECT_CALL(*mock_subprocess_, exit_code()).WillOnce(Return(-1));
  EXPECT_CALL(*mock_subprocess_, error_text()).WillOnce(Return(""));

  EXPECT_ERROR_CODE(::util::error::INTERNAL,
                    net_ns_config_->SetupInsideNamespace(spec));
}

TEST_F(NetNsConfiguratorTest, SetupInsideNamespace_Success) {
  NamespaceSpec spec;
  spec.mutable_net()->set_interface(kInterface);
  spec.mutable_net()->mutable_virtual_ip()->set_ip(kVip);
  spec.mutable_net()->mutable_virtual_ip()->set_netmask(kNetmask);
  spec.mutable_net()->mutable_virtual_ip()->set_gateway(kGateway);
  spec.mutable_net()->mutable_virtual_ip()->set_mtu(1500);
  spec.mutable_net()->mutable_virtual_ip()->set_ip_forward(true);

  const vector<string> command = CallActivateInterfaceCommand("lo");
  EXPECT_CALL(*mock_subprocess_, SetArgv(command));

  const vector<string> command2 = CallActivateInterfaceCommand(kInterface);
  EXPECT_CALL(*mock_subprocess_, SetArgv(command2));

  const vector<vector<string>> commands = CallConfigureNetworkInterfaceCommands(
      kInterface, spec.net().virtual_ip());
  for (const auto &argv : commands) {
    EXPECT_CALL(*mock_subprocess_, SetArgv(argv));
  }

  CommonMockSubProcessSetup();
  EXPECT_CALL(*mock_subprocess_, Communicate(NotNull(), NotNull()))
      .Times(6)
      .WillRepeatedly(Return(0));
  ASSERT_OK(net_ns_config_->SetupInsideNamespace(spec));
}

}  // namespace nscon
}  // namespace containers

