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
// Tests for UtsNsConfigurator class
//
#include "nscon/configurator/uts_ns_configurator.h"

#include <memory>

#include "nscon/ns_util_mock.h"
#include "include/namespaces.pb.h"
#include "util/errors_test_util.h"
#include "system_api/libc_net_api_test_util.h"
#include "gtest/gtest.h"
#include "util/task/status.h"

using ::std::unique_ptr;
using ::testing::StrEq;
using ::testing::Return;
using ::util::Status;

namespace containers {
namespace nscon {

class UtsNsConfiguratorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    mock_ns_util_.reset(new ::testing::StrictMock<MockNsUtil>());
    uts_ns_config_.reset(new UtsNsConfigurator(mock_ns_util_.get()));
  }

 protected:
  system_api::MockLibcNetApiOverride mock_libc_net_api_;
  unique_ptr<MockNsUtil> mock_ns_util_;
  unique_ptr<UtsNsConfigurator> uts_ns_config_;
};

TEST_F(UtsNsConfiguratorTest, SetupOutsideNamespace_Success) {
  NamespaceSpec spec;
  ASSERT_OK(uts_ns_config_->SetupOutsideNamespace(spec, 1));
}

TEST_F(UtsNsConfiguratorTest, SetupInsideNamespace_NoSpec) {
  NamespaceSpec spec;
  ASSERT_OK(uts_ns_config_->SetupInsideNamespace(spec));
}

TEST_F(UtsNsConfiguratorTest, SetupInsideNamespace_NoHostname) {
  NamespaceSpec spec;
  spec.mutable_uts();
  ASSERT_OK(uts_ns_config_->SetupInsideNamespace(spec));
}

TEST_F(UtsNsConfiguratorTest, SetupInsideNamespace_WithHostname_AFailure) {
  const string kHostname = "vhostname";
  NamespaceSpec spec;
  spec.mutable_uts()->set_vhostname(kHostname);
  EXPECT_CALL(mock_libc_net_api_.Mock(),
              SetHostname(StrEq(kHostname), kHostname.size()))
      .WillOnce(Return(-1));
  EXPECT_ERROR_CODE(::util::error::INTERNAL,
                    uts_ns_config_->SetupInsideNamespace(spec));
}

TEST_F(UtsNsConfiguratorTest, SetupInsideNamespace_WithHostname_Success) {
  const string kHostname = "vhostname";
  NamespaceSpec spec;
  spec.mutable_uts()->set_vhostname(kHostname);
  EXPECT_CALL(mock_libc_net_api_.Mock(),
              SetHostname(StrEq(kHostname), kHostname.size()))
      .WillOnce(Return(0));
  ASSERT_OK(uts_ns_config_->SetupInsideNamespace(spec));
}

}  // namespace nscon
}  // namespace containers
