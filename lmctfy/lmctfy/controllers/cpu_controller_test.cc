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

#include "lmctfy/controllers/cpu_controller.h"

#include <memory>

#include "system_api/kernel_api_mock.h"
#include "file/base/path.h"
#include "lmctfy/controllers/eventfd_notifications_mock.h"
#include "lmctfy/kernel_files.h"
#include "util/errors_test_util.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "strings/numbers.h"

using ::system_api::KernelAPIMock;
using ::file::JoinPath;
using ::std::unique_ptr;
using ::testing::NotNull;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::StrictMock;
using ::testing::_;
using ::util::Status;
using ::util::StatusOr;
using ::util::error::NOT_FOUND;

namespace containers {
namespace lmctfy {

static const char kMountPoint[] = "/dev/cgroup/cpu/test";
static const char kHierarchyPath[] = "/test";

class CpuControllerTest : public ::testing::Test {
 public:
  virtual void SetUp() {
    mock_kernel_.reset(new StrictMock<KernelAPIMock>());
    mock_eventfd_notifications_.reset(MockEventFdNotifications::NewStrict());
    controller_.reset(new CpuController(kHierarchyPath, kMountPoint, true,
                                        mock_kernel_.get(),
                                        mock_eventfd_notifications_.get()));
  }

 protected:
  unique_ptr<KernelAPIMock> mock_kernel_;
  unique_ptr<CpuController> controller_;
  unique_ptr<MockEventFdNotifications> mock_eventfd_notifications_;
};

TEST_F(CpuControllerTest, Type) {
  EXPECT_EQ(CGROUP_CPU, controller_->type());
}

TEST_F(CpuControllerTest, SetMilliCpus) {
  const string kResFile = JoinPath(kMountPoint, KernelFiles::Cpu::kShares);

  EXPECT_CALL(*mock_kernel_, SafeWriteResFile("1024", kResFile, NotNull(),
                                              NotNull())).WillOnce(Return(0));
  EXPECT_TRUE(controller_->SetMilliCpus(1000).ok());
}

TEST_F(CpuControllerTest, SetMilliCpusTooLow) {
  const string kResFile = JoinPath(kMountPoint, KernelFiles::Cpu::kShares);

  // Shares setting should not go below 2.
  EXPECT_CALL(*mock_kernel_, SafeWriteResFile("2", kResFile, NotNull(),
                                              NotNull())).WillOnce(Return(0));
  EXPECT_TRUE(controller_->SetMilliCpus(1).ok());
}


TEST_F(CpuControllerTest, SetMilliCpusFails) {
  const string kResFile = JoinPath(kMountPoint, KernelFiles::Cpu::kShares);

  EXPECT_CALL(*mock_kernel_,
              SafeWriteResFile("1024", kResFile, NotNull(), NotNull()))
      .WillOnce(DoAll(SetArgPointee<3>(true), Return(0)));
  EXPECT_FALSE(controller_->SetMilliCpus(1000).ok());
}

TEST_F(CpuControllerTest, SetMaxMilliCpus) {
  const string kQuotaFile = JoinPath(kMountPoint,
                                     KernelFiles::Cpu::kHardcapQuota);
  const string kPeriodFile = JoinPath(kMountPoint,
                                      KernelFiles::Cpu::kHardcapPeriod);

  StatusOr<int64> statusor = controller_->GetThrottlingPeriodInMs();
  ASSERT_TRUE(statusor.ok());
  const int64 milli_cpus = 2000;
  int64 expected_period_usecs = statusor.ValueOrDie() * 1000;
  int64 expected_quota_usecs = expected_period_usecs * (milli_cpus / 1000);
  EXPECT_CALL(*mock_kernel_, SafeWriteResFile(SimpleItoa(expected_quota_usecs),
                                              kQuotaFile, NotNull(), NotNull()))
      .WillOnce(Return(0));
  EXPECT_CALL(*mock_kernel_,
              SafeWriteResFile(SimpleItoa(expected_period_usecs), kPeriodFile,
                               NotNull(), NotNull())).WillOnce(Return(0));
  EXPECT_TRUE(controller_->SetMaxMilliCpus(milli_cpus).ok());
}


TEST_F(CpuControllerTest, SetMaxMilliCpusTooLow) {
  const string kQuotaFile = JoinPath(kMountPoint,
                                     KernelFiles::Cpu::kHardcapQuota);
  const string kPeriodFile = JoinPath(kMountPoint,
                                      KernelFiles::Cpu::kHardcapPeriod);

  EXPECT_FALSE(controller_->SetMaxMilliCpus(1).ok());
}

TEST_F(CpuControllerTest, SetMaxMilliCpusWritePeriodFails) {
  const string kPeriodFile = JoinPath(kMountPoint,
                                      KernelFiles::Cpu::kHardcapPeriod);

  EXPECT_CALL(*mock_kernel_,
              SafeWriteResFile("250000", kPeriodFile, NotNull(), NotNull()))
      .WillOnce(DoAll(SetArgPointee<3>(true), Return(0)));
  EXPECT_FALSE(controller_->SetMaxMilliCpus(2000).ok());
}


TEST_F(CpuControllerTest, SetMaxMilliCpusWriteQuotaFails) {
  const string kQuotaFile = JoinPath(kMountPoint,
                                     KernelFiles::Cpu::kHardcapQuota);
  const string kPeriodFile = JoinPath(kMountPoint,
                                      KernelFiles::Cpu::kHardcapPeriod);

  StatusOr<int64> statusor = controller_->GetThrottlingPeriodInMs();
  ASSERT_TRUE(statusor.ok());
  int64 expected_period_usecs = statusor.ValueOrDie() * 1000;
  EXPECT_CALL(*mock_kernel_,
              SafeWriteResFile(SimpleItoa(expected_period_usecs), kPeriodFile,
                               NotNull(), NotNull())).WillOnce(Return(0));
  EXPECT_CALL(*mock_kernel_,
              SafeWriteResFile("500000", kQuotaFile, NotNull(), NotNull()))
      .WillOnce(DoAll(SetArgPointee<3>(true), Return(0)));
  EXPECT_FALSE(controller_->SetMaxMilliCpus(2000).ok());
}

TEST_F(CpuControllerTest, SetLatencyPremier) {
  const string kLatFile = JoinPath(kMountPoint, KernelFiles::Cpu::kLatency);
  EXPECT_CALL(*mock_kernel_, SafeWriteResFile("25", kLatFile, NotNull(),
                                              NotNull())).WillOnce(Return(0));
  EXPECT_TRUE(controller_->SetLatency(PREMIER).ok());
}

TEST_F(CpuControllerTest, SetLatencyPriority) {
  const string kLatFile = JoinPath(kMountPoint, KernelFiles::Cpu::kLatency);
  EXPECT_CALL(*mock_kernel_, SafeWriteResFile("50", kLatFile, NotNull(),
                                              NotNull())).WillOnce(Return(0));
  EXPECT_TRUE(controller_->SetLatency(PRIORITY).ok());
}

TEST_F(CpuControllerTest, SetLatencyNormal) {
  const string kLatFile = JoinPath(kMountPoint, KernelFiles::Cpu::kLatency);
  EXPECT_CALL(*mock_kernel_, SafeWriteResFile("100", kLatFile, NotNull(),
                                              NotNull())).WillOnce(Return(0));
  EXPECT_TRUE(controller_->SetLatency(NORMAL).ok());
}

TEST_F(CpuControllerTest, SetLatencyBestEffort) {
  const string kLatFile = JoinPath(kMountPoint, KernelFiles::Cpu::kLatency);
  EXPECT_CALL(*mock_kernel_, SafeWriteResFile("-1", kLatFile, NotNull(),
                                              NotNull())).WillOnce(Return(0));
  EXPECT_TRUE(controller_->SetLatency(BEST_EFFORT).ok());
}

TEST_F(CpuControllerTest, SetLatencyFailure) {
  const string kLatFile = JoinPath(kMountPoint, KernelFiles::Cpu::kLatency);
  EXPECT_CALL(*mock_kernel_,
              SafeWriteResFile("25", kLatFile, NotNull(), NotNull()))
      .WillOnce(DoAll(SetArgPointee<3>(true), Return(0)));
  EXPECT_FALSE(controller_->SetLatency(PREMIER).ok());
}

TEST_F(CpuControllerTest, SetPlacementStrategy) {
  const string kResFile = JoinPath(kMountPoint,
                                   KernelFiles::Cpu::kPlacementStrategy);

  EXPECT_CALL(*mock_kernel_, SafeWriteResFile("401", kResFile, NotNull(),
                                              NotNull())).WillOnce(Return(0));
  EXPECT_TRUE(controller_->SetPlacementStrategy(401).ok());
}

TEST_F(CpuControllerTest, SetPlacementStrategyFails) {
  const string kResFile = JoinPath(kMountPoint,
                                   KernelFiles::Cpu::kPlacementStrategy);

  EXPECT_CALL(*mock_kernel_,
              SafeWriteResFile("401", kResFile, NotNull(), NotNull()))
      .WillOnce(DoAll(SetArgPointee<3>(true), Return(0)));
  EXPECT_FALSE(controller_->SetPlacementStrategy(401).ok());
}

TEST_F(CpuControllerTest, GetNumRunnable) {
  const string kResFile = JoinPath(kMountPoint,
                                   KernelFiles::Cpu::kNumRunning);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kResFile, NotNull()))
      .WillOnce(DoAll(SetArgPointee<1>("42"), Return(true)));

  StatusOr<int64> statusor = controller_->GetNumRunnable();
  ASSERT_TRUE(statusor.ok());
  EXPECT_EQ(42, statusor.ValueOrDie());
}

TEST_F(CpuControllerTest, GetNumRunnableNotFound) {
  const string kResFile = JoinPath(kMountPoint,
                                   KernelFiles::Cpu::kNumRunning);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK))
      .WillRepeatedly(Return(1));
  EXPECT_ERROR_CODE(NOT_FOUND, controller_->GetNumRunnable());
}

TEST_F(CpuControllerTest, GetNumRunnableFails) {
  const string kResFile = JoinPath(kMountPoint,
                                   KernelFiles::Cpu::kNumRunning);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kResFile, NotNull()))
      .WillOnce(Return(false));
  EXPECT_FALSE(controller_->GetNumRunnable().ok());
}

TEST_F(CpuControllerTest, GetMilliCpus) {
  const string kResFile = JoinPath(kMountPoint,
                                   KernelFiles::Cpu::kShares);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kResFile, NotNull()))
      .WillOnce(DoAll(SetArgPointee<1>("1024"), Return(true)));

  StatusOr<int64> statusor = controller_->GetMilliCpus();
  ASSERT_TRUE(statusor.ok());
  EXPECT_EQ(1000, statusor.ValueOrDie());
}

TEST_F(CpuControllerTest, GetMilliCpusNotFound) {
  const string kResFile = JoinPath(kMountPoint,
                                   KernelFiles::Cpu::kShares);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK))
      .WillRepeatedly(Return(1));
  EXPECT_ERROR_CODE(NOT_FOUND, controller_->GetMilliCpus());
}

TEST_F(CpuControllerTest, GetMilliCpusFails) {
  const string kResFile = JoinPath(kMountPoint,
                                   KernelFiles::Cpu::kShares);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kResFile, NotNull()))
      .WillOnce(Return(false));
  EXPECT_FALSE(controller_->GetMilliCpus().ok());
}

TEST_F(CpuControllerTest, GetMaxMilliCpus) {
  const string kResFile = JoinPath(kMountPoint,
                                   KernelFiles::Cpu::kHardcapQuota);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kResFile, NotNull()))
      .WillOnce(DoAll(SetArgPointee<1>("500000"), Return(true)));

  StatusOr<int64> statusor = controller_->GetMaxMilliCpus();
  ASSERT_TRUE(statusor.ok());
  EXPECT_EQ(2000, statusor.ValueOrDie());
}

TEST_F(CpuControllerTest, GetMaxMilliCpusUncapped) {
  const string kResFile = JoinPath(kMountPoint,
                                   KernelFiles::Cpu::kHardcapQuota);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kResFile, NotNull()))
      .WillOnce(DoAll(SetArgPointee<1>("-1"), Return(true)));

  StatusOr<int64> statusor = controller_->GetMaxMilliCpus();
  ASSERT_TRUE(statusor.ok());
  EXPECT_EQ(-1, statusor.ValueOrDie());
}

TEST_F(CpuControllerTest, GetMaxMilliCpusNotFound) {
  const string kResFile = JoinPath(kMountPoint,
                                   KernelFiles::Cpu::kHardcapQuota);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK))
      .WillRepeatedly(Return(1));
  EXPECT_ERROR_CODE(NOT_FOUND, controller_->GetMaxMilliCpus());
}

TEST_F(CpuControllerTest, GetMaxMilliCpusFails) {
  const string kResFile = JoinPath(kMountPoint,
                                   KernelFiles::Cpu::kHardcapQuota);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kResFile, NotNull()))
      .WillOnce(Return(false));
  EXPECT_FALSE(controller_->GetMaxMilliCpus().ok());
}


TEST_F(CpuControllerTest, GetLatencyBestEffort) {
  const string kResFile = JoinPath(kMountPoint, KernelFiles::Cpu::kLatency);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kResFile, NotNull()))
      .WillOnce(DoAll(SetArgPointee<1>("-1"), Return(true)));
  StatusOr<SchedulingLatency> statusor = controller_->GetLatency();
  ASSERT_TRUE(statusor.ok());
  EXPECT_EQ(BEST_EFFORT, statusor.ValueOrDie());
}

TEST_F(CpuControllerTest, GetLatencyNormal) {
  const string kResFile = JoinPath(kMountPoint, KernelFiles::Cpu::kLatency);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kResFile, NotNull()))
      .WillOnce(DoAll(SetArgPointee<1>("100"), Return(true)));
  StatusOr<SchedulingLatency> statusor = controller_->GetLatency();
  ASSERT_TRUE(statusor.ok());
  EXPECT_EQ(NORMAL, statusor.ValueOrDie());
}

TEST_F(CpuControllerTest, GetLatencyPriority) {
  const string kResFile = JoinPath(kMountPoint, KernelFiles::Cpu::kLatency);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kResFile, NotNull()))
      .WillOnce(DoAll(SetArgPointee<1>("50"), Return(true)));
  StatusOr<SchedulingLatency> statusor = controller_->GetLatency();
  ASSERT_TRUE(statusor.ok());
  EXPECT_EQ(PRIORITY, statusor.ValueOrDie());
}

TEST_F(CpuControllerTest, GetLatencyPremier) {
  const string kResFile = JoinPath(kMountPoint, KernelFiles::Cpu::kLatency);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kResFile, NotNull()))
      .WillOnce(DoAll(SetArgPointee<1>("25"), Return(true)));
  StatusOr<SchedulingLatency> statusor = controller_->GetLatency();
  ASSERT_TRUE(statusor.ok());
  EXPECT_EQ(PREMIER, statusor.ValueOrDie());
}

TEST_F(CpuControllerTest, GetLatencyFailureNotFound) {
  const string kResFile = JoinPath(kMountPoint, KernelFiles::Cpu::kLatency);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK))
      .WillRepeatedly(Return(1));
  EXPECT_ERROR_CODE(NOT_FOUND, controller_->GetLatency());
}

TEST_F(CpuControllerTest, GetLatencyFailure) {
  const string kResFile = JoinPath(kMountPoint, KernelFiles::Cpu::kLatency);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kResFile, NotNull()))
      .WillOnce(Return(false));
  EXPECT_FALSE(controller_->GetLatency().ok());
}

TEST_F(CpuControllerTest, GetThrottlingStats) {
  const string kResFile = JoinPath(kMountPoint,
                                   KernelFiles::Cpu::kThrottlingStats);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kResFile, NotNull()))
      .WillOnce(
          DoAll(SetArgPointee<1>(
              "nr_periods 2\nnr_throttled 1\nthrottled_time 200000000"),
                Return(true)));
  StatusOr<ThrottlingStats> statusor = controller_->GetThrottlingStats();
  ASSERT_TRUE(statusor.ok());
  EXPECT_EQ(statusor.ValueOrDie().nr_periods, 2);
  EXPECT_EQ(statusor.ValueOrDie().nr_throttled, 1);
  EXPECT_EQ(statusor.ValueOrDie().throttled_time, 200000000);
}


TEST_F(CpuControllerTest, GetThrottlingStatsIgnoresMalformedLines) {
  const string kResFile = JoinPath(kMountPoint,
                                   KernelFiles::Cpu::kThrottlingStats);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kResFile, NotNull()))
      .WillOnce(
          DoAll(SetArgPointee<1>(
              "This is a comment\nnr_periods 2\nnr_throttled 1\n"
              "throttled_time 200000000\n"),
                Return(true)));
  StatusOr<ThrottlingStats> statusor = controller_->GetThrottlingStats();
  ASSERT_TRUE(statusor.ok());
  EXPECT_EQ(statusor.ValueOrDie().nr_periods, 2);
  EXPECT_EQ(statusor.ValueOrDie().nr_throttled, 1);
  EXPECT_EQ(statusor.ValueOrDie().throttled_time, 200000000);
}

TEST_F(CpuControllerTest, GetThrottlingStatsIgnoresUnknownStats) {
  const string kResFile = JoinPath(kMountPoint,
                                   KernelFiles::Cpu::kThrottlingStats);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kResFile, NotNull()))
      .WillOnce(
          DoAll(SetArgPointee<1>(
              "nr_periods 2\nnr_throttled 1\nthrottled_time 200000000\n"
              "max_throttled 2000000\n"),
                Return(true)));
  StatusOr<ThrottlingStats> statusor = controller_->GetThrottlingStats();
  ASSERT_TRUE(statusor.ok());
  EXPECT_EQ(statusor.ValueOrDie().nr_periods, 2);
  EXPECT_EQ(statusor.ValueOrDie().nr_throttled, 1);
  EXPECT_EQ(statusor.ValueOrDie().throttled_time, 200000000);
}

TEST_F(CpuControllerTest, GetThrottlingStatsFailWithIncompleteStat) {
  const string kResFile = JoinPath(kMountPoint,
                                   KernelFiles::Cpu::kThrottlingStats);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kResFile, NotNull()))
      .WillOnce(
          DoAll(SetArgPointee<1>(
              "nr_periods 2\nnr_throttled 1\n"),
                Return(true)));
  EXPECT_FALSE(controller_->GetThrottlingStats().ok());
}

TEST_F(CpuControllerTest, GetThrottlingStatsNotFound) {
  const string kResFile = JoinPath(kMountPoint,
                                   KernelFiles::Cpu::kThrottlingStats);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK))
      .WillRepeatedly(Return(1));
  EXPECT_ERROR_CODE(NOT_FOUND, controller_->GetThrottlingStats());
}

TEST_F(CpuControllerTest, GetThrottlingStatsFailWithKernelReadFailure) {
  const string kResFile = JoinPath(kMountPoint,
                                   KernelFiles::Cpu::kThrottlingStats);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kResFile, NotNull()))
      .WillOnce(Return(false));
  EXPECT_FALSE(controller_->GetThrottlingStats().ok());
}

}  // namespace lmctfy
}  // namespace containers
