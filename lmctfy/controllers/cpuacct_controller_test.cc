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

#include "lmctfy/controllers/cpuacct_controller.h"

#include <limits.h>
#include <algorithm>
#include <map>
#include <memory>

#include "system_api/kernel_api_mock.h"
#include "file/base/path.h"
#include "lmctfy/controllers/eventfd_notifications_mock.h"
#include "lmctfy/kernel_files.h"
#include "util/errors_test_util.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "util/gtl/stl_util.h"
#include "util/task/codes.pb.h"

using ::system_api::KernelAPIMock;
using ::file::JoinPath;
using ::std::map;
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

static const char kMountPoint[] = "/dev/cgroup/cpuacct/test";
static const char kHierarchyPath[] = "/test";
static const char kQueueBuckets[] = "1000 5000 10000 25000 75000 100000 500000";
static const char kNonQueueBuckets[] =
    "1000 5000 10000 20000 50000 100000 250000";
static const char kProcHistogramPath[] = "/proc/sys/kernel/sched_histogram";

class CpuAcctControllerTest : public ::testing::Test {
 public:
  virtual void SetUp() {
    mock_kernel_.reset(new StrictMock<KernelAPIMock>());
    mock_eventfd_notifications_.reset(MockEventFdNotifications::NewStrict());
    controller_.reset(new CpuAcctController(kHierarchyPath, kMountPoint, true,
                                            mock_kernel_.get(),
                                            mock_eventfd_notifications_.get()));
  }

 protected:
  unique_ptr<KernelAPIMock> mock_kernel_;
  unique_ptr<CpuAcctController> controller_;
  unique_ptr<MockEventFdNotifications> mock_eventfd_notifications_;
};

TEST_F(CpuAcctControllerTest, Type) {
  EXPECT_EQ(CGROUP_CPUACCT, controller_->type());
}

TEST_F(CpuAcctControllerTest, GetCpuUsageInNs) {
  const string kResFile = JoinPath(kMountPoint, KernelFiles::CPUAcct::kUsage);

  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kResFile, NotNull()))
      .WillOnce(DoAll(SetArgPointee<1>("1000000000"), Return(true)));
  StatusOr<int64> statusor = controller_->GetCpuUsageInNs();
  ASSERT_TRUE(statusor.ok());
  EXPECT_EQ(1000000000, statusor.ValueOrDie());
}

TEST_F(CpuAcctControllerTest, GetCpuUsageInNsNotFound) {
  const string kResFile = JoinPath(kMountPoint, KernelFiles::CPUAcct::kUsage);

  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK))
      .WillRepeatedly(Return(1));
  EXPECT_ERROR_CODE(NOT_FOUND, controller_->GetCpuUsageInNs());
}

TEST_F(CpuAcctControllerTest, GetCpuUsageInNsFails) {
  const string kResFile = JoinPath(kMountPoint, KernelFiles::CPUAcct::kUsage);

  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kResFile, NotNull()))
      .WillOnce(Return(false));
  EXPECT_FALSE(controller_->GetCpuUsageInNs().ok());
}

TEST_F(CpuAcctControllerTest, GetPerCpuUsageInNs) {
  const string kResFile = JoinPath(kMountPoint,
                                   KernelFiles::CPUAcct::kUsagePerCPU);

  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kResFile, NotNull()))
      .WillOnce(DoAll(SetArgPointee<1>("0 1 2 3 4 5 6 7 8"), Return(true)));
  StatusOr<vector<int64>*> statusor = controller_->GetPerCpuUsageInNs();
  ASSERT_TRUE(statusor.ok());
  unique_ptr<vector<int64>> usage(statusor.ValueOrDie());
  ASSERT_EQ(9, usage->size());
  for (int i = 0; i < 9; ++i) {
    EXPECT_EQ(usage->at(i), i);
  }
}

TEST_F(CpuAcctControllerTest, GetPerCpuUsageInNsNotFound) {
  const string kResFile = JoinPath(kMountPoint,
                                   KernelFiles::CPUAcct::kUsagePerCPU);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK))
      .WillRepeatedly(Return(1));
  EXPECT_ERROR_CODE(NOT_FOUND, controller_->GetPerCpuUsageInNs());
}

TEST_F(CpuAcctControllerTest, GetPerCpuUsageInNsFails) {
  const string kResFile = JoinPath(kMountPoint,
                                   KernelFiles::CPUAcct::kUsagePerCPU);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kResFile, NotNull()))
      .WillOnce(Return(false));
  EXPECT_FALSE(controller_->GetPerCpuUsageInNs().ok());
}

TEST_F(CpuAcctControllerTest, GetCpuTime) {
  const string kResFile = JoinPath(kMountPoint,
                                   KernelFiles::CPUAcct::kStat);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kResFile, NotNull()))
      .WillOnce(DoAll(SetArgPointee<1>("user 1111\nsystem 121902102\n"),
                      Return(true)));
  StatusOr<CpuTime> statusor = controller_->GetCpuTime();
  ASSERT_TRUE(statusor.ok());
  CpuTime cpu_time = statusor.ValueOrDie();
  const int64 user_hz = sysconf(_SC_CLK_TCK);
  EXPECT_EQ(1111000000000 / user_hz, cpu_time.user.value());
  EXPECT_EQ(121902102000000000 / user_hz, cpu_time.system.value());
}

TEST_F(CpuAcctControllerTest, GetCpuTimeNotFound) {
  const string kResFile = JoinPath(kMountPoint,
                                   KernelFiles::CPUAcct::kStat);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK))
      .WillRepeatedly(Return(1));
  EXPECT_ERROR_CODE(NOT_FOUND, controller_->GetCpuTime());
}

TEST_F(CpuAcctControllerTest, GetCpuTimeFails) {
  const string kResFile = JoinPath(kMountPoint,
                                   KernelFiles::CPUAcct::kStat);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kResFile, NotNull()))
      .WillOnce(Return(false));
  EXPECT_FALSE(controller_->GetCpuTime().ok());
}

TEST_F(CpuAcctControllerTest, SetupHistograms) {
  const string kResFile = JoinPath(kMountPoint,
                                   KernelFiles::CPUAcct::kHistogram);
  EXPECT_CALL(*mock_kernel_,
              SafeWriteResFile("queue_self " + string(kQueueBuckets), kResFile,
                               NotNull(), NotNull())).WillOnce(Return(0));
  EXPECT_CALL(*mock_kernel_,
              SafeWriteResFile("queue_other " + string(kQueueBuckets), kResFile,
                               NotNull(), NotNull())).WillOnce(Return(0));
  EXPECT_CALL(*mock_kernel_,
              SafeWriteResFile("oncpu " + string(kNonQueueBuckets), kResFile,
                               NotNull(), NotNull())).WillOnce(Return(0));
  EXPECT_CALL(*mock_kernel_,
              SafeWriteResFile("serve " + string(kNonQueueBuckets), kResFile,
                               NotNull(), NotNull())).WillOnce(Return(0));
  EXPECT_CALL(*mock_kernel_,
              SafeWriteResFile("sleep " + string(kNonQueueBuckets), kResFile,
                               NotNull(), NotNull())).WillOnce(Return(0));
  EXPECT_TRUE(controller_->SetupHistograms().ok());
}

TEST_F(CpuAcctControllerTest, SetupHistogramsFails) {
  const string kResFile = JoinPath(kMountPoint,
                                   KernelFiles::CPUAcct::kHistogram);
  EXPECT_CALL(*mock_kernel_,
              SafeWriteResFile("queue_self " + string(kQueueBuckets), kResFile,
                               NotNull(), NotNull())).WillOnce(Return(0));
  EXPECT_CALL(*mock_kernel_,
              SafeWriteResFile("oncpu " + string(kNonQueueBuckets), kResFile,
                               NotNull(), NotNull())).WillOnce(Return(0));
  EXPECT_CALL(*mock_kernel_,
              SafeWriteResFile("serve " + string(kNonQueueBuckets), kResFile,
                               NotNull(), NotNull())).WillOnce(Return(0));
  EXPECT_CALL(*mock_kernel_,
              SafeWriteResFile("queue_other " + string(kQueueBuckets), kResFile,
                               NotNull(), NotNull())).WillOnce(Return(0));

  // Fail the last call.
  EXPECT_CALL(*mock_kernel_,
              SafeWriteResFile("sleep " + string(kNonQueueBuckets), kResFile,
                               NotNull(), NotNull()))
      .WillOnce(DoAll(SetArgPointee<3>(true), Return(0)));

  EXPECT_FALSE(controller_->SetupHistograms().ok());
}

TEST_F(CpuAcctControllerTest, EnableSchedulerHistograms) {
  EXPECT_CALL(*mock_kernel_,
              SafeWriteResFile("1", kProcHistogramPath, NotNull(), NotNull()))
      .WillOnce(Return(0));
  EXPECT_TRUE(controller_->EnableSchedulerHistograms().ok());
}

TEST_F(CpuAcctControllerTest, EnableSchedulerHistogramsFails) {
  EXPECT_CALL(*mock_kernel_,
              SafeWriteResFile("1", kProcHistogramPath, NotNull(), NotNull()))
      .WillOnce(DoAll(SetArgPointee<3>(true), Return(0)));
  EXPECT_FALSE(controller_->EnableSchedulerHistograms().ok());
}

static const char* kSampleHistogram = "unit: us\n"
    "serve\n"
    "bucket count\n"
    "< 1000  1675667\n< 5000  18153\n< 10000 3171\n"
    "< 20000 2168\n< 50000 3925\n< 100000  7\n"
    "< 250000  1\n< inf 0\n"
    "oncpu\n"
    "bucket count\n"
    "< 1000  1745563\n< 5000  27225\n< 10000 3989\n"
    "< 20000 4150\n< 50000 1618\n< 100000  0\n"
    "< 250000  0\n< inf 0\n"
    "queue_self\n"
    "bucket count\n"
    "< 25000 525857\n< 30000 465\n< 75000 72\n"
    "< 80000 0\n< 100000  1\n< 105000  0\n"
    "< 500000  0\n< inf 0\n"
    "queue_other\n"
    "bucket count\n"
    "< 25000 1000\n< 30000 0\n< 75000 0\n"
    "< 80000 0\n< 100000  0\n< 105000  0\n"
    "< 500000  0\n< inf 0\n"
    "sleep\n"
    "bucket count\n"
    "< 1000  314744\n< 5000  53349\n< 10000 12664\n"
    "< 20000 19889\n< 50000 308966\n< 100000  443933\n"
    "< 250000  153702\n< inf 0";

template <typename Map>
bool map_compare(Map const &left_map, Map const &right_map) {
  return left_map.size() == right_map.size()
      && std::equal(left_map.begin(), left_map.end(),
                    right_map.begin());
}

TEST_F(CpuAcctControllerTest, GetSchedulerHistograms) {
  const string kResFile = JoinPath(kMountPoint,
                                   KernelFiles::CPUAcct::kHistogram);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kResFile, NotNull()))
      .WillOnce(DoAll(SetArgPointee<1>(kSampleHistogram), Return(true)));
  StatusOr<vector<CpuHistogramData *>*> statusor =
      controller_->GetSchedulerHistograms();
  ASSERT_TRUE(statusor.ok());
  vector<CpuHistogramData *> *histograms = statusor.ValueOrDie();
  ASSERT_EQ(5, histograms->size());
  bool found_serve = false;
  bool found_oncpu = false;
  bool found_sleep = false;
  bool found_queue_self = false;
  bool found_queue_other = false;
  map<int, int64> expected_buckets;
  for (auto histo : *histograms) {
    switch (histo->type) {
    case SERVE:
      EXPECT_FALSE(found_serve);
      found_serve = true;
      expected_buckets.clear();
      expected_buckets[1000] = 1675667;
      expected_buckets[5000] = 18153;
      expected_buckets[10000] = 3171;
      expected_buckets[20000] = 2168;
      expected_buckets[50000] = 3925;
      expected_buckets[100000] = 7;
      expected_buckets[250000] = 1;
      expected_buckets[INT_MAX] = 0;
      EXPECT_TRUE(map_compare(expected_buckets, histo->buckets));
      break;
    case ONCPU:
      EXPECT_FALSE(found_oncpu);
      found_oncpu = true;
      expected_buckets.clear();
      expected_buckets[1000] = 1745563;
      expected_buckets[5000] = 27225;
      expected_buckets[10000] = 3989;
      expected_buckets[20000] = 4150;
      expected_buckets[50000] = 1618;
      expected_buckets[100000] = 0;
      expected_buckets[250000] = 0;
      expected_buckets[INT_MAX] = 0;
      EXPECT_TRUE(map_compare(expected_buckets, histo->buckets));
      break;
    case SLEEP:
      EXPECT_FALSE(found_sleep);
      found_sleep = true;
      expected_buckets.clear();
      expected_buckets[1000] = 314744;
      expected_buckets[5000] = 53349;
      expected_buckets[10000] = 12664;
      expected_buckets[20000] = 19889;
      expected_buckets[50000] = 308966;
      expected_buckets[100000] = 443933;
      expected_buckets[250000] = 153702;
      expected_buckets[INT_MAX] = 0;
      EXPECT_TRUE(map_compare(expected_buckets, histo->buckets));
      break;
    case QUEUE_SELF:
      EXPECT_FALSE(found_queue_self);
      found_queue_self = true;
      expected_buckets.clear();
      expected_buckets[25000] = 525857;
      expected_buckets[30000] = 465;
      expected_buckets[75000] = 72;
      expected_buckets[80000] = 0;
      expected_buckets[100000] = 1;
      expected_buckets[105000] = 0;
      expected_buckets[500000] = 0;
      expected_buckets[INT_MAX] = 0;
      EXPECT_TRUE(map_compare(expected_buckets, histo->buckets));
      break;
    case QUEUE_OTHER:
      EXPECT_FALSE(found_queue_other);
      found_queue_other = true;
      expected_buckets.clear();
      expected_buckets[25000] = 1000;
      expected_buckets[30000] = 0;
      expected_buckets[75000] = 0;
      expected_buckets[80000] = 0;
      expected_buckets[100000] = 0;
      expected_buckets[105000] = 0;
      expected_buckets[500000] = 0;
      expected_buckets[INT_MAX] = 0;
      EXPECT_TRUE(map_compare(expected_buckets, histo->buckets));
      break;
    default:
      EXPECT_FALSE(true);  // not reached.
      break;
    }
  }
  EXPECT_TRUE(found_serve);
  EXPECT_TRUE(found_oncpu);
  EXPECT_TRUE(found_sleep);
  EXPECT_TRUE(found_queue_self);
  EXPECT_TRUE(found_queue_other);

  STLDeleteElements(histograms);
  delete histograms;
}

TEST_F(CpuAcctControllerTest, GetSchedulerHistogramsNotFound) {
  const string kResFile = JoinPath(kMountPoint,
                                   KernelFiles::CPUAcct::kHistogram);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK))
      .WillRepeatedly(Return(1));
  EXPECT_ERROR_CODE(NOT_FOUND, controller_->GetSchedulerHistograms());
}

TEST_F(CpuAcctControllerTest, GetSchedulerHistogramsReadFails) {
  const string kResFile = JoinPath(kMountPoint,
                                   KernelFiles::CPUAcct::kHistogram);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kResFile, NotNull()))
      .WillOnce(Return(false));
  EXPECT_FALSE(controller_->GetSchedulerHistograms().ok());
}

static const char* kUnknownHistogram = "unit: us\n"
    "latency\n"
    "bucket count\n"
    "< 1000  1675667\n< 5000  18153\n< 10000 3171\n"
    "< 20000 2168\n< 50000 3925\n< 100000  7\n"
    "< 250000  1\n< inf 0\n";

TEST_F(CpuAcctControllerTest, GetSchedulerHistogramsUnknownHistogram) {
  const string kResFile = JoinPath(kMountPoint,
                                   KernelFiles::CPUAcct::kHistogram);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kResFile, NotNull()))
      .WillOnce(DoAll(SetArgPointee<1>(kUnknownHistogram), Return(true)));
  StatusOr<vector<CpuHistogramData *>*> statusor =
      controller_->GetSchedulerHistograms();
  EXPECT_FALSE(statusor.ok());
  EXPECT_EQ(::util::error::INTERNAL, statusor.status().error_code());
  EXPECT_EQ("Unknown histogram name \"latency\"",
            statusor.status().error_message());
}

// Missing histogram name.
static const char* kMalformedHistogram = "unit: us\n"
    "bucket count\n"
    "< 1000  1675667\n< 5000  18153\n< 10000 3171\n"
    "< 20000 2168\n< 50000 3925\n< 100000  7\n"
    "< 250000  1\n< inf 0\n";

TEST_F(CpuAcctControllerTest, GetSchedulerHistogramsMalformedHistogram) {
  const string kResFile = JoinPath(kMountPoint,
                                   KernelFiles::CPUAcct::kHistogram);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kResFile, NotNull()))
      .WillOnce(DoAll(SetArgPointee<1>(kMalformedHistogram), Return(true)));
  StatusOr<vector<CpuHistogramData *>*> statusor =
      controller_->GetSchedulerHistograms();
  EXPECT_FALSE(statusor.ok());
  EXPECT_EQ(::util::error::INTERNAL, statusor.status().error_code());
  EXPECT_EQ("Malformed histogram data.", statusor.status().error_message());
}

static const char* kGarbledBucket = "unit: us\n"
    "sleep\n"
    "bucket count\n"
    "< 1000  1675667\n< 5000  18153\n< 10000 3171\n"
    "< NaN 2168\n< 50000 3925\n< 100000  7\n"
    "< 250000  1\n< inf 0\n";

TEST_F(CpuAcctControllerTest, GetSchedulerHistogramsGarbledBucket) {
  const string kResFile = JoinPath(kMountPoint,
                                   KernelFiles::CPUAcct::kHistogram);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kResFile, NotNull()))
      .WillOnce(DoAll(SetArgPointee<1>(kGarbledBucket), Return(true)));
  StatusOr<vector<CpuHistogramData *>*> statusor =
      controller_->GetSchedulerHistograms();
  EXPECT_FALSE(statusor.ok());
  EXPECT_EQ(::util::error::INTERNAL, statusor.status().error_code());
  EXPECT_EQ("Failed to parse int from string \"NaN\"",
            statusor.status().error_message());
}

static const char* kGarbledValue = "unit: us\n"
    "sleep\n"
    "bucket count\n"
    "< 1000  1675667\n< 5000  18153\n< 10000 3171\n"
    "< 25000 NaN\n< 50000 3925\n< 100000  7\n"
    "< 250000  1\n< inf 0\n";

TEST_F(CpuAcctControllerTest, GetSchedulerHistogramsGarbledValue) {
  const string kResFile = JoinPath(kMountPoint,
                                   KernelFiles::CPUAcct::kHistogram);
  EXPECT_CALL(*mock_kernel_, Access(kResFile, F_OK))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(*mock_kernel_, ReadFileToString(kResFile, NotNull()))
      .WillOnce(DoAll(SetArgPointee<1>(kGarbledValue), Return(true)));
  StatusOr<vector<CpuHistogramData *>*> statusor =
      controller_->GetSchedulerHistograms();
  EXPECT_FALSE(statusor.ok());
  EXPECT_EQ(::util::error::INTERNAL, statusor.status().error_code());
  EXPECT_EQ("Failed to parse int from string \"NaN\"",
            statusor.status().error_message());
}

}  // namespace lmctfy
}  // namespace containers
