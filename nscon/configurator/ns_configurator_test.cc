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
// Test cases for NsConfigurator class
//
#include "nscon/configurator/ns_configurator.h"

#include <fcntl.h>
#include <memory>
#include <vector>

#include "nscon/ns_util_mock.h"
#include "include/namespaces.pb.h"
#include "util/errors_test_util.h"
#include "system_api/libc_process_api_test_util.h"
#include "system_api/libc_fs_api_test_util.h"
#include "gtest/gtest.h"

using ::std::unique_ptr;
using ::std::vector;
using ::util::error::INTERNAL;
using ::util::error::INVALID_ARGUMENT;
using ::util::error::UNKNOWN;
using ::util::Status;
using ::util::StatusOr;
#include "util/testing/equals_initialized_proto.h"
using ::testing::EqualsInitializedProto;
using ::testing::Return;
using ::testing::StrEq;

namespace containers {
namespace nscon {

const int kPid = 9999;
const int kNsFlag = CLONE_NEWPID;

class NsConfiguratorTest : public ::testing::Test {
 public:
  void SetUp() override {
    mock_ns_util_.reset(new ::testing::StrictMock<MockNsUtil>());
    ns_config_.reset(new NsConfigurator(kNsFlag, mock_ns_util_.get()));
  }

 protected:
  unique_ptr<MockNsUtil> mock_ns_util_;
  unique_ptr<NsConfigurator> ns_config_;
};

TEST_F(NsConfiguratorTest, SetupInsideNamespace_NoSpec) {
  NamespaceSpec spec;
  EXPECT_OK(ns_config_->SetupInsideNamespace(spec));
}

TEST_F(NsConfiguratorTest, SetupInsideNamespace_EmptySpec) {
  NamespaceSpec spec;
  spec.mutable_pid();
  EXPECT_OK(ns_config_->SetupInsideNamespace(spec));
}

TEST_F(NsConfiguratorTest, SetupOutsideNamspace_NoSpec) {
  NamespaceSpec spec;
  EXPECT_OK(ns_config_->SetupOutsideNamespace(spec, kPid));
}

TEST_F(NsConfiguratorTest, SetupOutsideNamspace_EmptySpec) {
  NamespaceSpec spec;
  spec.mutable_pid();
  EXPECT_OK(ns_config_->SetupOutsideNamespace(spec, kPid));
}

}  // namespace nscon
}  // namespace containers
