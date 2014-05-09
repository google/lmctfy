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

#include "lmctfy/controllers/freezer_controller.h"

#include <memory>

#include "system_api/kernel_api_mock.h"
#include "file/base/path.h"
#include "lmctfy/controllers/eventfd_notifications_mock.h"
#include "lmctfy/kernel_files.h"
#include "util/errors_test_util.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::system_api::KernelAPIMock;
using ::file::JoinPath;
using ::std::unique_ptr;
using ::testing::NotNull;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::StrEq;
using ::testing::StrictMock;
using ::util::error::INTERNAL;
using ::util::error::NOT_FOUND;
using ::util::Status;
using ::util::StatusOr;


namespace containers {
namespace lmctfy {

static const char kCgroupPath[] = "/dev/cgroup/freezer/test";
static const char kHierarchyPath[] = "/test";

class TestableFreezerController : public FreezerController {
 public:
  TestableFreezerController(const string &hierarchy_path,
                            const string &cgroup_path, bool owns_cgroup,
                            const KernelApi *kernel,
                            EventFdNotifications *eventfd_notifications)
      : FreezerController(hierarchy_path, cgroup_path, owns_cgroup, kernel,
                          eventfd_notifications) {}

  virtual ~TestableFreezerController() {}

  MOCK_CONST_METHOD0(GetSubcontainers,
                     ::util::StatusOr< ::std::vector<string>>());
  MOCK_CONST_METHOD1(GetParamInt,
                     ::util::StatusOr<int64>(const string &));
  MOCK_CONST_METHOD1(GetParamString,
                     ::util::StatusOr<string>(const string &));

  MOCK_METHOD2(SetParamString, ::util::Status(const string &, const string &));
};

class FreezerControllerTest : public ::testing::Test {
 public:
  FreezerControllerTest() :
      kFreezerStatusFile(KernelFiles::Freezer::kFreezerState),
      kFreezerParentFreezingFile(
          KernelFiles::Freezer::kFreezerParentFreezing) {}

  virtual ~FreezerControllerTest() {}

  virtual void SetUp() {
    mock_kernel_.reset(new ::testing::StrictMock<KernelAPIMock>());
    mock_eventfd_notifications_.reset(MockEventFdNotifications::NewStrict());
    controller_.reset(
        new TestableFreezerController(kHierarchyPath, kCgroupPath, false,
                                      mock_kernel_.get(),
                                      mock_eventfd_notifications_.get()));
  }


  void ExpectNoSubcontainers() {
    EXPECT_CALL(*controller_, GetSubcontainers())
        .WillOnce(Return(vector<string>(0)));
  }

  void ExpectSubcontainers() {
    EXPECT_CALL(*controller_, GetSubcontainers())
        .WillOnce(Return(vector<string>(1, "a")));
  }

  void ExpectHierarchicalFreezeNotSupported() {
    EXPECT_CALL(*controller_, GetParamInt(StrEq(kFreezerParentFreezingFile)))
        .WillOnce(Return(Status(NOT_FOUND, "Error")));
  }

  void ExpectHierarchicalFreezeSupported() {
    EXPECT_CALL(*controller_, GetParamInt(StrEq(kFreezerParentFreezingFile)))
        .WillOnce(Return(0));
  }

 protected:
  const string kFreezerStatusFile;
  const string kFreezerParentFreezingFile;

  unique_ptr<KernelAPIMock> mock_kernel_;
  unique_ptr<MockEventFdNotifications> mock_eventfd_notifications_;
  unique_ptr<TestableFreezerController> controller_;
};

TEST_F(FreezerControllerTest, Type) {
  EXPECT_EQ(CGROUP_FREEZER, controller_->type());
}

TEST_F(FreezerControllerTest, Freeze_HierarchicalNotSupported_NoSubcontainers) {
  ExpectHierarchicalFreezeNotSupported();
  ExpectNoSubcontainers();

  EXPECT_CALL(*controller_, SetParamString(StrEq(kFreezerStatusFile), "FROZEN"))
      .WillOnce(Return(Status::OK));
  EXPECT_OK(controller_->Freeze());
}

TEST_F(FreezerControllerTest, Freeze_HierarchicalSupported) {
  ExpectHierarchicalFreezeSupported();

  EXPECT_CALL(*controller_, SetParamString(StrEq(kFreezerStatusFile), "FROZEN"))
      .WillOnce(Return(Status::OK));

  EXPECT_OK(controller_->Freeze());
}

TEST_F(FreezerControllerTest, FreezeFails) {
  ExpectNoSubcontainers();
  ExpectHierarchicalFreezeNotSupported();

  EXPECT_CALL(*controller_, SetParamString(StrEq(kFreezerStatusFile), "FROZEN"))
      .WillOnce(Return(Status(INTERNAL, "blah blah blah")));

  EXPECT_NOT_OK(controller_->Freeze());
}

TEST_F(FreezerControllerTest,
       Freeze_HierarchicalNotSupported_WithSubcontainers) {
  ExpectHierarchicalFreezeNotSupported();
  ExpectSubcontainers();

  EXPECT_NOT_OK(controller_->Freeze());
}

TEST_F(FreezerControllerTest,
       Unfreeze_HierarchicalNotSupported_NoSubcontainers) {
  ExpectNoSubcontainers();
  ExpectHierarchicalFreezeNotSupported();

  EXPECT_CALL(*controller_, SetParamString(StrEq(kFreezerStatusFile), "THAWED"))
      .WillOnce(Return(Status::OK));
  EXPECT_OK(controller_->Unfreeze());
}

TEST_F(FreezerControllerTest, Unfreeze_HierarchicalSupported) {
  ExpectHierarchicalFreezeSupported();
  EXPECT_CALL(*controller_, SetParamString(StrEq(kFreezerStatusFile), "THAWED"))
      .WillOnce(Return(Status::OK));
  EXPECT_OK(controller_->Unfreeze());
}

TEST_F(FreezerControllerTest, UnfreezeFails) {
  ExpectNoSubcontainers();
  ExpectHierarchicalFreezeNotSupported();

  EXPECT_CALL(*controller_, SetParamString(StrEq(kFreezerStatusFile), "THAWED"))
      .WillOnce(Return(Status(INTERNAL, "System failure")));
  EXPECT_NOT_OK(controller_->Unfreeze());
}

TEST_F(FreezerControllerTest,
       Unfreeze_HierarchicalNotSupported_WithSubcontainers) {
  ExpectSubcontainers();
  ExpectHierarchicalFreezeNotSupported();

  EXPECT_NOT_OK(controller_->Unfreeze());
}

TEST_F(FreezerControllerTest, GetFreezerStateFrozen) {
  EXPECT_CALL(*controller_, GetParamString(StrEq(kFreezerStatusFile)))
      .WillOnce(Return(string("FROZEN")));
  StatusOr<FreezerState> statusor = controller_->State();
  ASSERT_OK(statusor);
  EXPECT_EQ(FreezerState::FROZEN, statusor.ValueOrDie());
}

TEST_F(FreezerControllerTest, GetFreezerStateThawed) {
  EXPECT_CALL(*controller_, GetParamString(StrEq(kFreezerStatusFile)))
      .WillOnce(Return(string("THAWED")));
  StatusOr<FreezerState> statusor = controller_->State();
  ASSERT_OK(statusor);
  EXPECT_EQ(FreezerState::THAWED, statusor.ValueOrDie());
}

TEST_F(FreezerControllerTest, GetFreezerStateFreezing) {
  EXPECT_CALL(*controller_, GetParamString(StrEq(kFreezerStatusFile)))
      .WillOnce(Return(string("FREEZING")));
  StatusOr<FreezerState> statusor = controller_->State();
  ASSERT_OK(statusor);
  EXPECT_EQ(FreezerState::FREEZING, statusor.ValueOrDie());
}

TEST_F(FreezerControllerTest, GetFreezerStateNotFound) {
  EXPECT_CALL(*controller_, GetParamString(StrEq(kFreezerStatusFile)))
      .WillOnce(Return(Status(NOT_FOUND, "Errrrr")));
  EXPECT_ERROR_CODE(::util::error::NOT_FOUND, controller_->State());
}

TEST_F(FreezerControllerTest, GetFreezerStateFails) {
  EXPECT_CALL(*controller_, GetParamString(StrEq(kFreezerStatusFile)))
      .WillOnce(Return(Status(INTERNAL, "Freezer?")));

  EXPECT_NOT_OK(controller_->State());
}

TEST_F(FreezerControllerTest, GetFreezerStateUnknown) {
  EXPECT_CALL(*controller_, GetParamString(StrEq(kFreezerStatusFile)))
      .WillOnce(Return(string("BROKEN")));

  EXPECT_ERROR_CODE(::util::error::INTERNAL, controller_->State());
}


}  // namespace lmctfy
}  // namespace containers
