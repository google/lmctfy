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

#include "lmctfy/resources/null_namespace_handler.h"

#include <sys/types.h>

#include <memory>
#include <string>
using ::std::string;

#include "base/callback.h"
#include "system_api/kernel_api_mock.h"
#include "include/lmctfy.h"
#include "include/lmctfy.pb.h"
#include "util/errors_test_util.h"
#include "gtest/gtest.h"
#include "util/process/mock_subprocess.h"
#include "util/task/codes.pb.h"
#include "util/task/status.h"
#include "util/task/statusor.h"

using ::std::unique_ptr;
using ::testing::StrictMock;
using ::testing::Return;
using ::util::UnixUid;
using ::util::UnixGid;
using ::util::Status;
using ::util::StatusOr;
using ::util::error::NOT_FOUND;
typedef ::system_api::KernelAPIMock MockKernelApi;

namespace containers {
namespace lmctfy {
namespace {

SubProcess *ReleaseSubProcess(unique_ptr<MockSubProcess> *subprocess) {
  return subprocess->release();
}

class NullNamespaceHandlerFactoryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    null_namespace_handler_factory_.reset(
        new NullNamespaceHandlerFactory(&mock_kernel_));
  }

  unique_ptr<NullNamespaceHandlerFactory> null_namespace_handler_factory_;

 private:
  StrictMock<MockKernelApi> mock_kernel_;
};

TEST_F(NullNamespaceHandlerFactoryTest, GetNamespaceHandlerNonRootNotFound) {
  EXPECT_ERROR_CODE(
      ::util::error::NOT_FOUND,
      null_namespace_handler_factory_->GetNamespaceHandler("/some_container"));
}

TEST_F(NullNamespaceHandlerFactoryTest, GetNamespaceHandlerRootFound) {
  auto statusor = null_namespace_handler_factory_->GetNamespaceHandler("/");
  ASSERT_OK(statusor);
  delete statusor.ValueOrDie();
}

class NullNamespaceHandlerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    mock_subprocess_.reset(new StrictMock<MockSubProcess>());
    subprocess_factory_.reset(NewPermanentCallback(&ReleaseSubProcess,
                                                   &mock_subprocess_));
    null_namespace_handler_.reset(new NullNamespaceHandler(
        kContainerName,
        &mock_kernel_,
        subprocess_factory_.get()));
  }

  unique_ptr<NullNamespaceHandler> null_namespace_handler_;
  const string kContainerName = "some_name";
  StrictMock<MockKernelApi> mock_kernel_;
  unique_ptr<MockSubProcess> mock_subprocess_;

 private:
  unique_ptr<ResultCallback<SubProcess *>> subprocess_factory_;
};

TEST_F(NullNamespaceHandlerTest, CreateResource) {
  EXPECT_OK(null_namespace_handler_->CreateResource(ContainerSpec()));
}

TEST_F(NullNamespaceHandlerTest, UpdateDiffNoop) {
  EXPECT_OK(null_namespace_handler_->Update(ContainerSpec(),
                                            Container::UPDATE_DIFF));
}

TEST_F(NullNamespaceHandlerTest, UpdateReplaceNoop) {
  EXPECT_OK(null_namespace_handler_->Update(ContainerSpec(),
                                            Container::UPDATE_DIFF));
}

// Tests for Exec().

TEST_F(NullNamespaceHandlerTest, ExecSuccess) {
  const vector<string> kCmd = {"/bin/echo", "test", "cmd"};

  EXPECT_CALL(mock_kernel_, SetITimer(ITIMER_REAL, nullptr, nullptr))
      .WillOnce(Return(0));
  EXPECT_CALL(mock_kernel_, SetITimer(ITIMER_VIRTUAL, nullptr, nullptr))
      .WillOnce(Return(0));
  EXPECT_CALL(mock_kernel_, SetITimer(ITIMER_PROF, nullptr, nullptr))
      .WillOnce(Return(0));
  EXPECT_CALL(mock_kernel_, Execvp(kCmd[0], kCmd))
      .WillOnce(Return(0));

  // We expect INTERNAL since Exec does not typically return on success.
  EXPECT_ERROR_CODE(::util::error::INTERNAL,
                    null_namespace_handler_->Exec(kCmd));
}

TEST_F(NullNamespaceHandlerTest, ExecSetITimerFails) {
  const vector<string> kCmd = {"/bin/echo", "test", "cmd"};

  EXPECT_CALL(mock_kernel_, SetITimer(ITIMER_REAL, nullptr, nullptr))
      .WillOnce(Return(-1));
  EXPECT_CALL(mock_kernel_, SetITimer(ITIMER_VIRTUAL, nullptr, nullptr))
      .WillOnce(Return(-1));
  EXPECT_CALL(mock_kernel_, SetITimer(ITIMER_PROF, nullptr, nullptr))
      .WillOnce(Return(-1));
  EXPECT_CALL(mock_kernel_, Execvp(kCmd[0], kCmd))
      .WillOnce(Return(0));

  // We expect INTERNAL since Exec does not typically return on success and we
  // ignore the failure of SetITimer.
  EXPECT_ERROR_CODE(::util::error::INTERNAL,
                    null_namespace_handler_->Exec(kCmd));
}

TEST_F(NullNamespaceHandlerTest, ExecExecvpFails) {
  const vector<string> kCmd = {"/bin/echo", "test", "cmd"};

  EXPECT_CALL(mock_kernel_, SetITimer(ITIMER_REAL, nullptr, nullptr))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(mock_kernel_, SetITimer(ITIMER_VIRTUAL, nullptr, nullptr))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(mock_kernel_, SetITimer(ITIMER_PROF, nullptr, nullptr))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(mock_kernel_, Execvp(kCmd[0], kCmd))
      .WillOnce(Return(1));

  EXPECT_ERROR_CODE(::util::error::INTERNAL,
                    null_namespace_handler_->Exec(kCmd));
}

static const pid_t kTid = 42;
static const pid_t kPid = 22;

TEST_F(NullNamespaceHandlerTest, RunNoCommandDetached) {
  const vector<string> kCmd = {};
  RunSpec spec;
  spec.set_fd_policy(RunSpec::DETACHED);
  EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT,
                    null_namespace_handler_->Run(kCmd, spec));
}

TEST_F(NullNamespaceHandlerTest, RunNoCommandInherit) {
  const vector<string> kCmd = {};
  RunSpec spec;
  spec.set_fd_policy(RunSpec::INHERIT);
  EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT,
                    null_namespace_handler_->Run(kCmd, spec));
}

TEST_F(NullNamespaceHandlerTest, RunUnknownFdPolicy) {
  RunSpec spec;
  spec.set_fd_policy(RunSpec::UNKNOWN);
  EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT,
                    null_namespace_handler_->Run({"/bin/true"}, spec));
}

TEST_F(NullNamespaceHandlerTest, RunSuccessBackground) {
  const vector<string> kCmd = {"/bin/echo", "test", "cmd"};

  EXPECT_CALL(*mock_subprocess_, SetArgv(kCmd))
      .Times(1);
  EXPECT_CALL(*mock_subprocess_, SetUseSession())
      .Times(1);
  EXPECT_CALL(*mock_subprocess_, Start())
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_subprocess_, pid())
      .WillRepeatedly(Return(kPid));

  RunSpec spec;
  spec.set_fd_policy(RunSpec::DETACHED);
  StatusOr<pid_t> statusor = null_namespace_handler_->Run(kCmd, spec);
  ASSERT_TRUE(statusor.ok());
  EXPECT_EQ(kPid, statusor.ValueOrDie());
}

TEST_F(NullNamespaceHandlerTest, RunSuccessForeground) {
  const vector<string> kCmd = {"/bin/echo", "test", "cmd"};

  EXPECT_CALL(*mock_subprocess_, SetArgv(kCmd))
      .Times(1);
  EXPECT_CALL(*mock_subprocess_,
              SetChannelAction(CHAN_STDIN, ACTION_DUPPARENT))
      .Times(1);
  EXPECT_CALL(*mock_subprocess_,
              SetChannelAction(CHAN_STDOUT, ACTION_DUPPARENT))
      .Times(1);
  EXPECT_CALL(*mock_subprocess_,
              SetChannelAction(CHAN_STDERR, ACTION_DUPPARENT))
      .Times(1);
  EXPECT_CALL(*mock_subprocess_, Start())
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_subprocess_, pid())
      .WillRepeatedly(Return(kPid));

  RunSpec spec;
  spec.set_fd_policy(RunSpec::INHERIT);
  StatusOr<pid_t> statusor = null_namespace_handler_->Run(kCmd, spec);
  ASSERT_TRUE(statusor.ok());
  EXPECT_EQ(kPid, statusor.ValueOrDie());
}

TEST_F(NullNamespaceHandlerTest, RunSuccessDefaultPolicy) {
  const vector<string> kCmd = {"/bin/echo", "test", "cmd"};

  EXPECT_CALL(*mock_subprocess_, SetArgv(kCmd))
      .Times(1);
  EXPECT_CALL(*mock_subprocess_,
              SetChannelAction(CHAN_STDIN, ACTION_DUPPARENT))
      .Times(1);
  EXPECT_CALL(*mock_subprocess_,
              SetChannelAction(CHAN_STDOUT, ACTION_DUPPARENT))
      .Times(1);
  EXPECT_CALL(*mock_subprocess_,
              SetChannelAction(CHAN_STDERR, ACTION_DUPPARENT))
      .Times(1);
  EXPECT_CALL(*mock_subprocess_, Start())
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_subprocess_, pid())
      .WillRepeatedly(Return(kPid));

  // Inherit is the default policy.
  RunSpec spec;
  StatusOr<pid_t> statusor = null_namespace_handler_->Run(kCmd, spec);
  ASSERT_TRUE(statusor.ok());
  EXPECT_EQ(kPid, statusor.ValueOrDie());
}

TEST_F(NullNamespaceHandlerTest, RunStartProcessFails) {
  const vector<string> kCmd = {"/bin/echo", "test", "cmd"};

  EXPECT_CALL(*mock_subprocess_, SetArgv(kCmd))
      .Times(1);
  EXPECT_CALL(*mock_subprocess_, SetUseSession())
      .Times(1);
  EXPECT_CALL(*mock_subprocess_, Start())
      .WillOnce(Return(false));

  RunSpec spec;
  spec.set_fd_policy(RunSpec::DETACHED);
  StatusOr<pid_t> statusor = null_namespace_handler_->Run(kCmd, spec);
  EXPECT_FALSE(statusor.ok());
  EXPECT_EQ(::util::error::FAILED_PRECONDITION, statusor.status().error_code());
}

TEST_F(NullNamespaceHandlerTest, StatsFullNoop) {
  ContainerStats stats;
  EXPECT_OK(null_namespace_handler_->Stats(Container::STATS_FULL, &stats));
}

TEST_F(NullNamespaceHandlerTest, StatsSummaryNoop) {
  ContainerStats stats;
  EXPECT_OK(null_namespace_handler_->Stats(Container::STATS_SUMMARY, &stats));
}

TEST_F(NullNamespaceHandlerTest, Spec) {
  ContainerSpec spec;
  EXPECT_OK(null_namespace_handler_->Spec(&spec));
}

TEST_F(NullNamespaceHandlerTest, Destroy) {
  EXPECT_OK(null_namespace_handler_.release()->Destroy());
}

TEST_F(NullNamespaceHandlerTest, Delegate) {
  const UnixUid kUid(42);
  const UnixGid kGid(42);
  EXPECT_OK(null_namespace_handler_->Delegate(kUid, kGid));
}

TEST_F(NullNamespaceHandlerTest, NotificationsNotFound) {
  EventSpec spec;
  EXPECT_ERROR_CODE(
      ::util::error::NOT_FOUND,
      null_namespace_handler_->RegisterNotification(spec, nullptr));
}

}  // namespace
}  // namespace lmctfy
}  // namespace containers

