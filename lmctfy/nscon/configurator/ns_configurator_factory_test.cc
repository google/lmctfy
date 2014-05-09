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
// Tests for NsConfiguratorFactory class
//
#include "nscon/configurator/ns_configurator_factory.h"

#include <memory>

#include "nscon/configurator/ns_configurator.h"
#include "nscon/ns_util_mock.h"
#include "util/errors_test_util.h"
#include "gtest/gtest.h"
#include "util/task/status.h"
#include "util/task/statusor.h"

using ::std::unique_ptr;
using ::testing::Return;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace nscon {

class NsConfiguratorFactoryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    mock_ns_util_.reset(new ::testing::StrictMock<MockNsUtil>());
    EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT,
                      NsConfiguratorFactory::New(nullptr));
    StatusOr<NsConfiguratorFactory *> ns_config =
        NsConfiguratorFactory::New(mock_ns_util_.get());
    ASSERT_OK(ns_config);
    ns_config_.reset(ns_config.ValueOrDie());
  }

  unique_ptr<NsConfiguratorFactory> ns_config_;
  unique_ptr<MockNsUtil> mock_ns_util_;
};

TEST_F(NsConfiguratorFactoryTest, Get_InvalidNsFlag) {
  const int kCloneFlag = CLONE_VFORK;
  EXPECT_CALL(*mock_ns_util_, NsCloneFlagToName(kCloneFlag))
      .WillOnce(Return(Status(::util::error::INVALID_ARGUMENT, "")));
  EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT,
                    ns_config_->Get(kCloneFlag));
}

TEST_F(NsConfiguratorFactoryTest, Get_UnimplementedNamespace) {
  const int kCloneFlag = 0x8080;
  EXPECT_CALL(*mock_ns_util_, NsCloneFlagToName(kCloneFlag))
      .WillOnce(Return("foo"));
  EXPECT_ERROR_CODE(::util::error::NOT_FOUND,
                    ns_config_->Get(kCloneFlag));
}

TEST_F(NsConfiguratorFactoryTest, Get_Success) {
  const int kCloneFlag = CLONE_NEWPID;
  EXPECT_CALL(*mock_ns_util_, NsCloneFlagToName(kCloneFlag))
      .WillOnce(Return("pid"));
  StatusOr<NsConfigurator *> configurator = ns_config_->Get(kCloneFlag);
  ASSERT_OK(configurator);
  delete configurator.ValueOrDie();
}

}  // namespace nscon
}  // namespace containers

