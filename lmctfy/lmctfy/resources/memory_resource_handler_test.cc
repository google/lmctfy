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

#include "lmctfy/resources/memory_resource_handler.h"

#include <memory>
#include <vector>

#include "base/integral_types.h"
#include "system_api/kernel_api_mock.h"
#include "lmctfy/controllers/cgroup_factory_mock.h"
#include "lmctfy/controllers/eventfd_notifications_mock.h"
#include "lmctfy/controllers/memory_controller_mock.h"
#include "lmctfy/resource_handler.h"
#include "include/lmctfy.pb.h"
#include "util/safe_types/bytes.h"
#include "util/errors_test_util.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "util/task/codes.pb.h"
#include "util/task/status.h"

using ::system_api::KernelAPIMock;
using ::util::Bytes;
using ::std::unique_ptr;
using ::std::vector;
using ::testing::NiceMock;
using ::testing::NotNull;
using ::testing::Return;
using ::testing::StrictMock;
using ::testing::_;
using ::util::Status;
using ::util::StatusOr;
using ::util::error::NOT_FOUND;

namespace containers {
namespace lmctfy {

static const char kContainerName[] = "/test";

class MemoryResourceHandlerFactoryTest : public ::testing::Test {
 public:
  virtual void SetUp() {
    mock_kernel_.reset(new StrictMock<KernelAPIMock>());
    mock_controller_ = new StrictMockMemoryController();
    mock_cgroup_factory_.reset(new NiceMockCgroupFactory());
    mock_controller_factory_ =
        new StrictMockMemoryControllerFactory(mock_cgroup_factory_.get());
    factory_.reset(new MemoryResourceHandlerFactory(mock_controller_factory_,
                                                    mock_cgroup_factory_.get(),
                                                    mock_kernel_.get()));
  }

  // Wrappers over private methods for testing.

  StatusOr<ResourceHandler *> CallGetResourceHandler(
      const string &container_name) {
    return factory_->GetResourceHandler(container_name);
  }

  StatusOr<ResourceHandler *> CallCreateResourceHandler(
      const string &container_name, const ContainerSpec &spec) {
    return factory_->CreateResourceHandler(container_name, spec);
  }

 protected:
  MockMemoryController *mock_controller_;
  MockMemoryControllerFactory *mock_controller_factory_;
  unique_ptr<KernelAPIMock> mock_kernel_;
  unique_ptr<MemoryResourceHandlerFactory> factory_;
  unique_ptr<MockCgroupFactory> mock_cgroup_factory_;
};

// Tests for New().

TEST_F(MemoryResourceHandlerFactoryTest, NewSuccess) {
  unique_ptr<MockEventFdNotifications> mock_notifications(
      MockEventFdNotifications::NewStrict());

  EXPECT_CALL(*mock_cgroup_factory_, IsMounted(_))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_cgroup_factory_, OwnsCgroup(_))
      .WillRepeatedly(Return(true));

  StatusOr<ResourceHandlerFactory *> statusor =
      MemoryResourceHandlerFactory::New(mock_cgroup_factory_.get(),
                                        mock_kernel_.get(),
                                        mock_notifications.get());
  ASSERT_OK(statusor);
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  delete statusor.ValueOrDie();
}

TEST_F(MemoryResourceHandlerFactoryTest, NewNotMounted) {
  unique_ptr<MockEventFdNotifications> mock_notifications(
      MockEventFdNotifications::NewStrict());

  EXPECT_CALL(*mock_cgroup_factory_, IsMounted(_))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*mock_cgroup_factory_, OwnsCgroup(_))
      .WillRepeatedly(Return(true));

  StatusOr<ResourceHandlerFactory *> statusor =
      MemoryResourceHandlerFactory::New(mock_cgroup_factory_.get(),
                                        mock_kernel_.get(),
                                        mock_notifications.get());
  EXPECT_ERROR_CODE(::util::error::NOT_FOUND, statusor);
}

// Tests for Get().

TEST_F(MemoryResourceHandlerFactoryTest, GetSuccess) {
  EXPECT_CALL(*mock_controller_factory_, Get(kContainerName))
      .WillRepeatedly(Return(mock_controller_));

  StatusOr<ResourceHandler *> statusor = CallGetResourceHandler(kContainerName);
  ASSERT_TRUE(statusor.ok());
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  unique_ptr<ResourceHandler> handler(statusor.ValueOrDie());
  EXPECT_EQ(RESOURCE_MEMORY, handler->type());
  EXPECT_EQ(kContainerName, handler->container_name());
}

TEST_F(MemoryResourceHandlerFactoryTest, GetFails) {
  EXPECT_CALL(*mock_controller_factory_, Get(kContainerName))
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED, CallGetResourceHandler(kContainerName).status());

  // Mock controller was not used.
  delete mock_controller_;
}

// Tests for Create().

TEST_F(MemoryResourceHandlerFactoryTest, CreateSuccess) {
  ContainerSpec spec;

  EXPECT_CALL(*mock_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(mock_controller_));

  StatusOr<ResourceHandler *> statusor =
      CallCreateResourceHandler(kContainerName, spec);
  ASSERT_TRUE(statusor.ok());
  EXPECT_NE(nullptr, statusor.ValueOrDie());
  unique_ptr<ResourceHandler> handler(statusor.ValueOrDie());
  EXPECT_EQ(RESOURCE_MEMORY, handler->type());
  EXPECT_EQ(kContainerName, handler->container_name());
}

TEST_F(MemoryResourceHandlerFactoryTest, CreateFails) {
  ContainerSpec spec;

  EXPECT_CALL(*mock_controller_factory_, Create(kContainerName))
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED,
            CallCreateResourceHandler(kContainerName, spec).status());

  // Mock controller was not used.
  delete mock_controller_;
}

class MemoryResourceHandlerTest : public ::testing::Test {
 public:
  MemoryResourceHandlerTest()
      : kStatTypes({Container::STATS_SUMMARY, Container::STATS_FULL}) {}

  virtual void SetUp() {
    mock_kernel_.reset(new StrictMock<KernelAPIMock>());
    mock_memory_controller_ = new StrictMockMemoryController();
    handler_.reset(new MemoryResourceHandler(
        kContainerName, mock_kernel_.get(),
        mock_memory_controller_));

    EXPECT_CALL(*mock_memory_controller_, GetWorkingSet())
        .WillRepeatedly(Return(Bytes(1)));
    EXPECT_CALL(*mock_memory_controller_, GetUsage())
        .WillRepeatedly(Return(Bytes(2)));
    EXPECT_CALL(*mock_memory_controller_, GetMaxUsage())
        .WillRepeatedly(Return(Bytes(3)));
    EXPECT_CALL(*mock_memory_controller_, GetLimit())
        .WillRepeatedly(Return(Bytes(4)));
    EXPECT_CALL(*mock_memory_controller_, GetEffectiveLimit())
        .WillRepeatedly(Return(Bytes(5)));
    EXPECT_CALL(*mock_memory_controller_, GetSoftLimit())
        .WillRepeatedly(Return(Bytes(6)));
    memory_stats_status_ = Status::OK;
    memory_stats_.reset(new MemoryStats());
    memory_stats_->mutable_container_data()->set_cache(7);
    EXPECT_CALL(*mock_memory_controller_, GetMemoryStats(_)).WillRepeatedly(
        testing::Invoke(this, &MemoryResourceHandlerTest::PopulateMemoryStats));

    numa_stats_status_ = Status::OK;
    numa_stats_.reset(new MemoryStats_NumaStats());
    numa_stats_->mutable_container_data()->mutable_total()
        ->set_total_page_count(8);
    EXPECT_CALL(*mock_memory_controller_, GetNumaStats(_)).WillRepeatedly(
        testing::Invoke(this, &MemoryResourceHandlerTest::PopulateNumaStats));

    idle_page_stats_status_ = Status::OK;
    idle_page_stats_.reset(new MemoryStats_IdlePageStats());
    idle_page_stats_->set_scans(9);
    EXPECT_CALL(*mock_memory_controller_, GetIdlePageStats(_)).WillRepeatedly(
        testing::Invoke(this,
                        &MemoryResourceHandlerTest::PopulateIdlePageStats));

    compression_sampling_stats_status_ = Status::OK;
    compression_sampling_stats_.reset(
        new MemoryStats_CompressionSamplingStats());
    compression_sampling_stats_->set_raw_size(10);
    EXPECT_CALL(*mock_memory_controller_,
                GetCompressionSamplingStats(_)).WillRepeatedly(
                    testing::Invoke(this,
                        &MemoryResourceHandlerTest::
                            PopulateCompressionSamplingStats));
    EXPECT_CALL(*mock_memory_controller_, GetFailCount())
        .WillRepeatedly(Return(11));
  }

  Status memory_stats_status_;
  unique_ptr<MemoryStats> memory_stats_;

  Status numa_stats_status_;
  unique_ptr<MemoryStats_NumaStats> numa_stats_;

  Status idle_page_stats_status_;
  unique_ptr<MemoryStats_IdlePageStats> idle_page_stats_;

  Status compression_sampling_stats_status_;
  unique_ptr<MemoryStats_CompressionSamplingStats> compression_sampling_stats_;

 protected:
  Status PopulateMemoryStats(MemoryStats *stats) const {
    if (memory_stats_status_.ok()) {
      stats->mutable_container_data()->CopyFrom(
          memory_stats_->container_data());
      stats->mutable_hierarchical_data()->CopyFrom(
          memory_stats_->hierarchical_data());
      stats->set_hierarchical_memory_limit(
          memory_stats_->hierarchical_memory_limit());
    }
    return memory_stats_status_;
  }

  Status PopulateNumaStats(MemoryStats_NumaStats *stats) const {
    if (numa_stats_status_.ok()) {
      stats->CopyFrom(*numa_stats_);
    }
    return numa_stats_status_;
  }

  Status PopulateIdlePageStats(MemoryStats_IdlePageStats *stats) const {
    if (idle_page_stats_status_.ok()) {
      stats->CopyFrom(*idle_page_stats_);
    }
    return idle_page_stats_status_;
  }

  Status PopulateCompressionSamplingStats(
      MemoryStats_CompressionSamplingStats *stats) const {
    if (compression_sampling_stats_status_.ok()) {
      stats->CopyFrom(*compression_sampling_stats_);
    }
    return compression_sampling_stats_status_;
  }

  const vector<Container::StatsType> kStatTypes;

  MockMemoryController *mock_memory_controller_;
  unique_ptr<KernelAPIMock> mock_kernel_;
  unique_ptr<MemoryResourceHandler> handler_;
};

// Tests for CreateOnlySetup()

TEST_F(MemoryResourceHandlerTest, CreateOnlySetupSucceeds) {
  ContainerSpec spec;

  EXPECT_CALL(*mock_memory_controller_, SetStalePageAge(1))
      .WillOnce(Return(Status::OK));

  EXPECT_OK(handler_->CreateOnlySetup(spec));
}

TEST_F(MemoryResourceHandlerTest, CreateOnlySetupSetStalePageAgeNotSupported) {
  ContainerSpec spec;

  EXPECT_CALL(*mock_memory_controller_, SetStalePageAge(1))
      .WillOnce(Return(Status(::util::error::NOT_FOUND, "")));

  EXPECT_OK(handler_->CreateOnlySetup(spec));
}

TEST_F(MemoryResourceHandlerTest, CreateOnlySetupSetStalePageAgeFails) {
  ContainerSpec spec;

  EXPECT_CALL(*mock_memory_controller_, SetStalePageAge(1))
      .WillOnce(Return(Status::CANCELLED));

  EXPECT_NOT_OK(handler_->CreateOnlySetup(spec));
}

// Tests for Stats()

TEST_F(MemoryResourceHandlerTest, StatsSuccess) {
  for (Container::StatsType type : kStatTypes) {
    ContainerStats stats;

    EXPECT_TRUE(handler_->Stats(type, &stats).ok());
    ASSERT_TRUE(stats.has_memory());
    EXPECT_EQ(1, stats.memory().working_set());
    EXPECT_EQ(2, stats.memory().usage());
    EXPECT_EQ(3, stats.memory().max_usage());
    EXPECT_EQ(4, stats.memory().limit());
    EXPECT_EQ(5, stats.memory().effective_limit());
    EXPECT_EQ(6, stats.memory().reservation());
    EXPECT_EQ(7, stats.memory().container_data().cache());
    EXPECT_FALSE(stats.memory().hierarchical_data().has_cache());
    EXPECT_EQ(8, stats.memory().numa().container_data().total()
                     .total_page_count());
    EXPECT_EQ(9, stats.memory().idle_page().scans());
    EXPECT_EQ(10, stats.memory().compression_sampling().raw_size());
    EXPECT_EQ(11, stats.memory().fail_count());
  }
}

TEST_F(MemoryResourceHandlerTest, StatsGetWorkingSetFails) {
  for (Container::StatsType type : kStatTypes) {
    ContainerStats stats;

    EXPECT_CALL(*mock_memory_controller_, GetWorkingSet())
        .WillRepeatedly(Return(Status::CANCELLED));

    EXPECT_EQ(Status::CANCELLED, handler_->Stats(type, &stats));
  }
}

TEST_F(MemoryResourceHandlerTest, StatsGetUsage) {
  for (Container::StatsType type : kStatTypes) {
    ContainerStats stats;

    EXPECT_CALL(*mock_memory_controller_, GetUsage())
        .WillRepeatedly(Return(Status::CANCELLED));

    EXPECT_EQ(Status::CANCELLED, handler_->Stats(type, &stats));
  }
}

TEST_F(MemoryResourceHandlerTest, StatsGetMaxUsageFails) {
  for (Container::StatsType type : kStatTypes) {
    ContainerStats stats;

    EXPECT_CALL(*mock_memory_controller_, GetMaxUsage())
        .WillRepeatedly(Return(Status::CANCELLED));

    EXPECT_EQ(Status::CANCELLED, handler_->Stats(type, &stats));
  }
}

TEST_F(MemoryResourceHandlerTest, StatsGetLimitFails) {
  for (Container::StatsType type : kStatTypes) {
    ContainerStats stats;

    EXPECT_CALL(*mock_memory_controller_, GetLimit())
        .WillRepeatedly(Return(Status::CANCELLED));

    EXPECT_EQ(Status::CANCELLED, handler_->Stats(type, &stats));
  }
}

TEST_F(MemoryResourceHandlerTest, StatsGetEffectiveLimitFails) {
  for (Container::StatsType type : kStatTypes) {
    ContainerStats stats;

    EXPECT_CALL(*mock_memory_controller_, GetEffectiveLimit())
        .WillRepeatedly(Return(Status::CANCELLED));

    EXPECT_EQ(Status::CANCELLED, handler_->Stats(type, &stats));
  }
}

TEST_F(MemoryResourceHandlerTest, StatsGetSoftLimitFails) {
  for (Container::StatsType type : kStatTypes) {
    ContainerStats stats;

    EXPECT_CALL(*mock_memory_controller_, GetSoftLimit())
        .WillRepeatedly(Return(Status::CANCELLED));

    EXPECT_EQ(Status::CANCELLED, handler_->Stats(type, &stats));
  }
}

TEST_F(MemoryResourceHandlerTest, StatsGetMemoryStatsFails) {
  memory_stats_status_ = Status::CANCELLED;
  for (Container::StatsType type : kStatTypes) {
    ContainerStats stats;

    EXPECT_EQ(Status::CANCELLED, handler_->Stats(type, &stats));
  }
}

TEST_F(MemoryResourceHandlerTest, StatsGetIdlePageStatsFails) {
  idle_page_stats_status_ = Status::CANCELLED;
  for (Container::StatsType type : kStatTypes) {
    ContainerStats stats;

    EXPECT_EQ(Status::CANCELLED, handler_->Stats(type, &stats));
  }
}

TEST_F(MemoryResourceHandlerTest, StatsGetFailCountFails) {
  for (Container::StatsType type : kStatTypes) {
    ContainerStats stats;

    EXPECT_CALL(*mock_memory_controller_, GetFailCount())
        .WillRepeatedly(Return(Status::CANCELLED));

    EXPECT_EQ(Status::CANCELLED, handler_->Stats(type, &stats));
  }
}

// Tests for Update().

TEST_F(MemoryResourceHandlerTest, UpdateDiffEmpty) {
  ContainerSpec spec;

  EXPECT_TRUE(handler_->Update(spec, Container::UPDATE_DIFF).ok());
}

TEST_F(MemoryResourceHandlerTest, UpdateDiffWithLimitSuccess) {
  ContainerSpec spec;
  spec.mutable_memory()->set_limit(42);

  EXPECT_CALL(*mock_memory_controller_, SetLimit(Bytes(42)))
      .WillOnce(Return(Status::OK));

  EXPECT_TRUE(handler_->Update(spec, Container::UPDATE_DIFF).ok());
}

TEST_F(MemoryResourceHandlerTest, UpdateDiffWithLimitFails) {
  ContainerSpec spec;
  spec.mutable_memory()->set_limit(42);

  EXPECT_CALL(*mock_memory_controller_, SetLimit(Bytes(42)))
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED, handler_->Update(spec, Container::UPDATE_DIFF));
}

TEST_F(MemoryResourceHandlerTest, UpdateDiffWithSwapLimitSuccess) {
  ContainerSpec spec;
  spec.mutable_memory()->set_swap_limit(42);

  EXPECT_CALL(*mock_memory_controller_, SetSwapLimit(Bytes(42)))
      .WillOnce(Return(Status::OK));

  EXPECT_OK(handler_->Update(spec, Container::UPDATE_DIFF));
}

TEST_F(MemoryResourceHandlerTest, UpdateDiffWithSwapLimitFails) {
  ContainerSpec spec;
  spec.mutable_memory()->set_swap_limit(42);

  EXPECT_CALL(*mock_memory_controller_, SetSwapLimit(Bytes(42)))
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED, handler_->Update(spec, Container::UPDATE_DIFF));
}

TEST_F(MemoryResourceHandlerTest, UpdateDiffWithReservationSuccess) {
  ContainerSpec spec;
  spec.mutable_memory()->set_reservation(42);

  EXPECT_CALL(*mock_memory_controller_, SetSoftLimit(Bytes(42)))
      .WillOnce(Return(Status::OK));

  EXPECT_TRUE(handler_->Update(spec, Container::UPDATE_DIFF).ok());
}

TEST_F(MemoryResourceHandlerTest, UpdateDiffWithReservationFails) {
  ContainerSpec spec;
  spec.mutable_memory()->set_reservation(42);

  EXPECT_CALL(*mock_memory_controller_, SetSoftLimit(Bytes(42)))
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED, handler_->Update(spec, Container::UPDATE_DIFF));
}

TEST_F(MemoryResourceHandlerTest,
       UpdateDiffWithStalePageAgeSuccess) {
  ContainerSpec spec;
  spec.mutable_memory()->set_stale_page_age(42);
  EXPECT_CALL(*mock_memory_controller_, SetStalePageAge(42))
      .WillOnce(Return(Status::OK));
  EXPECT_TRUE(handler_->Update(spec, Container::UPDATE_DIFF).ok());
}

TEST_F(MemoryResourceHandlerTest,
       UpdateDiffWithStalePageAgeFails) {
  ContainerSpec spec;
  spec.mutable_memory()->set_stale_page_age(42);
  EXPECT_CALL(*mock_memory_controller_, SetStalePageAge(42))
      .WillRepeatedly(Return(Status::CANCELLED));
  EXPECT_EQ(Status::CANCELLED, handler_->Update(spec, Container::UPDATE_DIFF));
}

TEST_F(MemoryResourceHandlerTest, UpdateDiffSetDirtyRatiosSuccess) {
  ContainerSpec spec;
  spec.mutable_memory()->mutable_dirty()->set_ratio(42);
  spec.mutable_memory()->mutable_dirty()->set_background_ratio(43);
  EXPECT_CALL(*mock_memory_controller_, SetDirtyRatio(42))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_memory_controller_, SetDirtyBackgroundRatio(43))
      .WillOnce(Return(Status::OK));
  EXPECT_OK(handler_->Update(spec, Container::UPDATE_DIFF));
}

TEST_F(MemoryResourceHandlerTest, UpdateDiffSetDirtyRatioOnly) {
  ContainerSpec spec;
  spec.mutable_memory()->mutable_dirty()->set_ratio(42);
  EXPECT_CALL(*mock_memory_controller_, SetDirtyRatio(42))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_memory_controller_, SetDirtyBackgroundRatio(_)).Times(0);
  EXPECT_OK(handler_->Update(spec, Container::UPDATE_DIFF));
}

TEST_F(MemoryResourceHandlerTest, UpdateDiffSetDirtyBackgroundRatioOnly) {
  ContainerSpec spec;
  spec.mutable_memory()->mutable_dirty()->set_background_ratio(43);
  EXPECT_CALL(*mock_memory_controller_, SetDirtyRatio(_)).Times(0);
  EXPECT_CALL(*mock_memory_controller_, SetDirtyBackgroundRatio(43))
      .WillOnce(Return(Status::OK));
  EXPECT_OK(handler_->Update(spec, Container::UPDATE_DIFF));
}

TEST_F(MemoryResourceHandlerTest, UpdateDiffSetDirtyRatioFails) {
  ContainerSpec spec;
  spec.mutable_memory()->mutable_dirty()->set_ratio(42);
  spec.mutable_memory()->mutable_dirty()->set_background_ratio(43);
  EXPECT_CALL(*mock_memory_controller_, SetDirtyRatio(42))
      .WillRepeatedly(Return(Status::CANCELLED));
  EXPECT_CALL(*mock_memory_controller_, SetDirtyBackgroundRatio(43))
      .WillRepeatedly(Return(Status::OK));
  EXPECT_NOT_OK(handler_->Update(spec, Container::UPDATE_DIFF));
}

TEST_F(MemoryResourceHandlerTest, UpdateDiffSetDirtyRatioNotFound) {
  ContainerSpec spec;
  spec.mutable_memory()->mutable_dirty()->set_ratio(42);
  spec.mutable_memory()->mutable_dirty()->set_background_ratio(43);
  EXPECT_CALL(*mock_memory_controller_, SetDirtyRatio(42))
      .WillRepeatedly(Return(Status(NOT_FOUND, "")));
  EXPECT_CALL(*mock_memory_controller_, SetDirtyBackgroundRatio(43))
      .WillRepeatedly(Return(Status::OK));
  EXPECT_ERROR_CODE(NOT_FOUND, handler_->Update(spec, Container::UPDATE_DIFF));
}

TEST_F(MemoryResourceHandlerTest, UpdateDiffSetDirtyBackgroundRatioFails) {
  ContainerSpec spec;
  spec.mutable_memory()->mutable_dirty()->set_ratio(42);
  spec.mutable_memory()->mutable_dirty()->set_background_ratio(43);
  EXPECT_CALL(*mock_memory_controller_, SetDirtyRatio(42))
      .WillRepeatedly(Return(Status::OK));
  EXPECT_CALL(*mock_memory_controller_, SetDirtyBackgroundRatio(43))
      .WillRepeatedly(Return(Status::CANCELLED));
  EXPECT_NOT_OK(handler_->Update(spec, Container::UPDATE_DIFF));
}

TEST_F(MemoryResourceHandlerTest, UpdateDiffSetDirtyBackgroundRatioNotFound) {
  ContainerSpec spec;
  spec.mutable_memory()->mutable_dirty()->set_ratio(42);
  spec.mutable_memory()->mutable_dirty()->set_background_ratio(43);
  EXPECT_CALL(*mock_memory_controller_, SetDirtyRatio(42))
      .WillRepeatedly(Return(Status::OK));
  EXPECT_CALL(*mock_memory_controller_, SetDirtyBackgroundRatio(43))
      .WillRepeatedly(Return(Status(NOT_FOUND, "")));
  EXPECT_ERROR_CODE(NOT_FOUND, handler_->Update(spec, Container::UPDATE_DIFF));
}

TEST_F(MemoryResourceHandlerTest, UpdateDiffSetDirtyLimitSuccess) {
  ContainerSpec spec;
  spec.mutable_memory()->mutable_dirty()->set_limit(42);
  spec.mutable_memory()->mutable_dirty()->set_background_limit(43);
  EXPECT_CALL(*mock_memory_controller_, SetDirtyLimit(Bytes(42)))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_memory_controller_, SetDirtyBackgroundLimit(Bytes(43)))
      .WillOnce(Return(Status::OK));
  EXPECT_OK(handler_->Update(spec, Container::UPDATE_DIFF));
}

TEST_F(MemoryResourceHandlerTest, UpdateDiffSetDirtyLimitOnly) {
  ContainerSpec spec;
  spec.mutable_memory()->mutable_dirty()->set_limit(42);
  EXPECT_CALL(*mock_memory_controller_, SetDirtyLimit(Bytes(42)))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_memory_controller_, SetDirtyBackgroundLimit(_)).Times(0);
  EXPECT_OK(handler_->Update(spec, Container::UPDATE_DIFF));
}

TEST_F(MemoryResourceHandlerTest, UpdateDiffSetDirtyBackgroundLimitOnly) {
  ContainerSpec spec;
  spec.mutable_memory()->mutable_dirty()->set_background_limit(43);
  EXPECT_CALL(*mock_memory_controller_, SetDirtyLimit(_)).Times(0);
  EXPECT_CALL(*mock_memory_controller_, SetDirtyBackgroundLimit(Bytes(43)))
      .WillOnce(Return(Status::OK));
  EXPECT_OK(handler_->Update(spec, Container::UPDATE_DIFF));
}

TEST_F(MemoryResourceHandlerTest, UpdateDiffSetDirtyLimitFails) {
  ContainerSpec spec;
  spec.mutable_memory()->mutable_dirty()->set_limit(42);
  spec.mutable_memory()->mutable_dirty()->set_background_limit(43);
  EXPECT_CALL(*mock_memory_controller_, SetDirtyLimit(Bytes(42)))
      .WillRepeatedly(Return(Status::CANCELLED));
  EXPECT_CALL(*mock_memory_controller_, SetDirtyBackgroundLimit(Bytes(43)))
      .WillRepeatedly(Return(Status::OK));
  EXPECT_NOT_OK(handler_->Update(spec, Container::UPDATE_DIFF));
}

TEST_F(MemoryResourceHandlerTest, UpdateDiffSetDirtyLimitNotFound) {
  ContainerSpec spec;
  spec.mutable_memory()->mutable_dirty()->set_limit(42);
  spec.mutable_memory()->mutable_dirty()->set_background_limit(43);
  EXPECT_CALL(*mock_memory_controller_, SetDirtyLimit(Bytes(42)))
      .WillRepeatedly(Return(Status(NOT_FOUND, "")));
  EXPECT_CALL(*mock_memory_controller_, SetDirtyBackgroundLimit(Bytes(43)))
      .WillRepeatedly(Return(Status::OK));
  EXPECT_ERROR_CODE(NOT_FOUND, handler_->Update(spec, Container::UPDATE_DIFF));
}

TEST_F(MemoryResourceHandlerTest, UpdateDiffSetDirtyBackgroundLimitFails) {
  ContainerSpec spec;
  spec.mutable_memory()->mutable_dirty()->set_limit(42);
  spec.mutable_memory()->mutable_dirty()->set_background_limit(43);
  EXPECT_CALL(*mock_memory_controller_, SetDirtyLimit(Bytes(42)))
      .WillRepeatedly(Return(Status::OK));
  EXPECT_CALL(*mock_memory_controller_, SetDirtyBackgroundLimit(Bytes(43)))
      .WillRepeatedly(Return(Status::CANCELLED));
  EXPECT_NOT_OK(handler_->Update(spec, Container::UPDATE_DIFF));
}

TEST_F(MemoryResourceHandlerTest, UpdateDiffSetDirtyBackgroundLimitNotFound) {
  ContainerSpec spec;
  spec.mutable_memory()->mutable_dirty()->set_limit(42);
  spec.mutable_memory()->mutable_dirty()->set_background_limit(43);
  EXPECT_CALL(*mock_memory_controller_, SetDirtyLimit(Bytes(42)))
      .WillRepeatedly(Return(Status::OK));
  EXPECT_CALL(*mock_memory_controller_, SetDirtyBackgroundLimit(Bytes(43)))
      .WillRepeatedly(Return(Status(NOT_FOUND, "")));
  EXPECT_ERROR_CODE(NOT_FOUND, handler_->Update(spec, Container::UPDATE_DIFF));
}

TEST_F(MemoryResourceHandlerTest, InvalidDirtyValues) {
  ContainerSpec spec;
  spec.mutable_memory()->mutable_dirty()->set_ratio(42);
  spec.mutable_memory()->mutable_dirty()->set_background_ratio(43);
  spec.mutable_memory()->mutable_dirty()->set_limit(44);
  spec.mutable_memory()->mutable_dirty()->set_background_limit(45);
  EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT,
                    handler_->Update(spec, Container::UPDATE_DIFF));
}

TEST_F(MemoryResourceHandlerTest, UpdateDiffKMemChargeUsageSucceeds) {
  ContainerSpec spec;
  spec.mutable_memory()->set_kmem_charge_usage(true);
  EXPECT_CALL(*mock_memory_controller_, SetKMemChargeUsage(true))
      .WillRepeatedly(Return(Status::OK));
  EXPECT_OK(handler_->Update(spec, Container::UPDATE_DIFF));
}

TEST_F(MemoryResourceHandlerTest, UpdateDiffKMemChargeUsageFails) {
  ContainerSpec spec;
  spec.mutable_memory()->set_kmem_charge_usage(true);
  EXPECT_CALL(*mock_memory_controller_, SetKMemChargeUsage(true))
      .WillRepeatedly(Return(Status::CANCELLED));
  EXPECT_NOT_OK(handler_->Update(spec, Container::UPDATE_DIFF));
}

class MemoryResourceUpdateReplaceTest : public MemoryResourceHandlerTest {
 public:
  virtual void SetUp() {
    MemoryResourceHandlerTest::SetUp();
    // Set the default values for the updates we're not looking at for the test
    EXPECT_CALL(*mock_memory_controller_, SetLimit(_))
        .WillRepeatedly(Return(Status::OK));
    EXPECT_CALL(*mock_memory_controller_, SetSwapLimit(_))
        .WillRepeatedly(Return(Status::OK));
    EXPECT_CALL(*mock_memory_controller_, SetSoftLimit(_))
        .WillRepeatedly(Return(Status::OK));
    EXPECT_CALL(*mock_memory_controller_, SetOomScore(_))
        .WillRepeatedly(Return(Status::OK));
    EXPECT_CALL(*mock_memory_controller_, SetCompressionSamplingRatio(_))
        .WillRepeatedly(Return(Status::OK));
    EXPECT_CALL(*mock_memory_controller_, SetStalePageAge(_))
        .WillRepeatedly(Return(Status::OK));
    EXPECT_CALL(*mock_memory_controller_, SetDirtyRatio(_))
        .WillRepeatedly(Return(Status::OK));
    EXPECT_CALL(*mock_memory_controller_, SetDirtyBackgroundRatio(_))
        .WillRepeatedly(Return(Status::OK));
    EXPECT_CALL(*mock_memory_controller_, SetKMemChargeUsage(_))
        .WillRepeatedly(Return(Status::OK));
  }
};

TEST_F(MemoryResourceUpdateReplaceTest, Empty) {
  ContainerSpec spec;

  EXPECT_CALL(*mock_memory_controller_, SetLimit(Bytes(-1)))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_memory_controller_, SetSwapLimit(Bytes(-1)))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_memory_controller_, SetSoftLimit(Bytes(0)))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_memory_controller_, SetOomScore(5000))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_memory_controller_, SetCompressionSamplingRatio(0))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_memory_controller_, SetStalePageAge(1))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_memory_controller_, SetDirtyRatio(75))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_memory_controller_, SetDirtyBackgroundRatio(10))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_memory_controller_, SetKMemChargeUsage(false))
      .WillOnce(Return(Status::OK));

  EXPECT_TRUE(handler_->Update(spec, Container::UPDATE_REPLACE).ok());
}

TEST_F(MemoryResourceUpdateReplaceTest, WithLimitSuccess) {
  ContainerSpec spec;
  spec.mutable_memory()->set_limit(42);

  EXPECT_CALL(*mock_memory_controller_, SetLimit(Bytes(42)))
      .WillOnce(Return(Status::OK));

  EXPECT_TRUE(handler_->Update(spec, Container::UPDATE_REPLACE).ok());
}

TEST_F(MemoryResourceUpdateReplaceTest, WithLimitSetLimitFails) {
  ContainerSpec spec;
  spec.mutable_memory()->set_limit(42);

  EXPECT_CALL(*mock_memory_controller_, SetLimit(Bytes(42)))
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED,
            handler_->Update(spec, Container::UPDATE_REPLACE));
}

TEST_F(MemoryResourceUpdateReplaceTest, WithSetSwapLimitSuccess) {
  ContainerSpec spec;
  spec.mutable_memory()->set_swap_limit(42);

  EXPECT_CALL(*mock_memory_controller_, SetSwapLimit(Bytes(42)))
      .WillOnce(Return(Status::OK));

  EXPECT_OK(handler_->Update(spec, Container::UPDATE_REPLACE));
}

TEST_F(MemoryResourceUpdateReplaceTest, WithSetSwapLimitFails) {
  ContainerSpec spec;
  spec.mutable_memory()->set_swap_limit(42);

  EXPECT_CALL(*mock_memory_controller_, SetSwapLimit(Bytes(42)))
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED,
            handler_->Update(spec, Container::UPDATE_REPLACE));
}

TEST_F(MemoryResourceUpdateReplaceTest, WithSetSwapLimitNotSupported) {
  ContainerSpec spec;

  EXPECT_CALL(*mock_memory_controller_, SetSwapLimit(Bytes(-1)))
      .WillOnce(Return(Status(::util::error::NOT_FOUND, "")));

  EXPECT_OK(handler_->Update(spec, Container::UPDATE_REPLACE));
}

TEST_F(MemoryResourceUpdateReplaceTest, WithLimitSetReservationFails) {
  ContainerSpec spec;

  EXPECT_CALL(*mock_memory_controller_, SetSoftLimit(Bytes(0)))
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED,
            handler_->Update(spec, Container::UPDATE_REPLACE));
}

TEST_F(MemoryResourceUpdateReplaceTest, WithLimitSetOomScoreNotSupported) {
  ContainerSpec spec;

  EXPECT_CALL(*mock_memory_controller_, SetOomScore(5000))
      .WillOnce(Return(Status(::util::error::NOT_FOUND, "")));

  EXPECT_OK(handler_->Update(spec, Container::UPDATE_REPLACE));
}

TEST_F(MemoryResourceUpdateReplaceTest, WithLimitAndReservationSuccess) {
  ContainerSpec spec;
  spec.mutable_memory()->set_limit(42);
  spec.mutable_memory()->set_reservation(43);

  EXPECT_CALL(*mock_memory_controller_, SetLimit(Bytes(42)))
      .WillRepeatedly(Return(Status::OK));
  EXPECT_CALL(*mock_memory_controller_, SetSoftLimit(Bytes(43)))
      .WillRepeatedly(Return(Status::OK));

  EXPECT_TRUE(handler_->Update(spec, Container::UPDATE_REPLACE).ok());
}

TEST_F(MemoryResourceUpdateReplaceTest,
       WithLimitAndReservationSetReservationFails) {
  ContainerSpec spec;
  spec.mutable_memory()->set_limit(42);
  spec.mutable_memory()->set_reservation(43);

  EXPECT_CALL(*mock_memory_controller_, SetLimit(Bytes(42)))
      .WillRepeatedly(Return(Status::OK));
  EXPECT_CALL(*mock_memory_controller_, SetSoftLimit(Bytes(43)))
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED,
            handler_->Update(spec, Container::UPDATE_REPLACE));
}

TEST_F(MemoryResourceUpdateReplaceTest, WithEvictionPrioritySuccess) {
  ContainerSpec spec;
  spec.mutable_memory()->set_eviction_priority(1000);

  EXPECT_CALL(*mock_memory_controller_, SetOomScore(1000))
      .WillOnce(Return(Status::OK));

  EXPECT_OK(handler_->Update(spec, Container::UPDATE_REPLACE));
}

TEST_F(MemoryResourceUpdateReplaceTest, WithEvictionPrioritySetOomScoreFails) {
  ContainerSpec spec;
  spec.mutable_memory()->set_eviction_priority(1000);

  EXPECT_CALL(*mock_memory_controller_, SetOomScore(1000))
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED,
            handler_->Update(spec, Container::UPDATE_REPLACE));
}

TEST_F(MemoryResourceUpdateReplaceTest, WithNegativeEvictionPriorityFails) {
  ContainerSpec spec;
  spec.mutable_memory()->set_eviction_priority(-100);

  EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT,
                    handler_->Update(spec, Container::UPDATE_REPLACE));
}

TEST_F(MemoryResourceUpdateReplaceTest, WithOutOfRangeEvictionPriorityFails) {
  ContainerSpec spec;
  spec.mutable_memory()->set_eviction_priority(20000);

  EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT,
                    handler_->Update(spec, Container::UPDATE_REPLACE));
}

TEST_F(MemoryResourceUpdateReplaceTest, WithEvictionPriorityNotFound) {
  ContainerSpec spec;
  spec.mutable_memory()->set_eviction_priority(1000);

  EXPECT_CALL(*mock_memory_controller_, SetOomScore(1000))
      .WillRepeatedly(Return(Status(::util::error::NOT_FOUND, "")));

  EXPECT_OK(handler_->Update(spec, Container::UPDATE_REPLACE));
}

TEST_F(MemoryResourceUpdateReplaceTest, WithEvictionPriorityOtherError) {
  ContainerSpec spec;
  spec.mutable_memory()->set_eviction_priority(1000);

  EXPECT_CALL(*mock_memory_controller_, SetOomScore(1000))
      .WillRepeatedly(Return(Status(::util::error::INVALID_ARGUMENT, "")));

  EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT,
                    handler_->Update(spec, Container::UPDATE_REPLACE));
}

TEST_F(MemoryResourceUpdateReplaceTest, WithCompressionSamplingRatioSucceeds) {
  ContainerSpec spec;
  spec.mutable_memory()->set_compression_sampling_ratio(42);
  EXPECT_CALL(*mock_memory_controller_, SetCompressionSamplingRatio(42))
      .WillRepeatedly(Return(Status::OK));
  EXPECT_OK(handler_->Update(spec, Container::UPDATE_REPLACE));
}

TEST_F(MemoryResourceUpdateReplaceTest, WithCompressionSamplingRatioFails) {
  ContainerSpec spec;
  spec.mutable_memory()->set_compression_sampling_ratio(42);
  EXPECT_CALL(*mock_memory_controller_, SetCompressionSamplingRatio(42))
      .WillRepeatedly(Return(Status(::util::error::INVALID_ARGUMENT, "")));
  EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT,
                    handler_->Update(spec, Container::UPDATE_REPLACE));
}

TEST_F(MemoryResourceUpdateReplaceTest, WithStalePageAgeSucceeds) {
  ContainerSpec spec;
  spec.mutable_memory()->set_stale_page_age(42);
  EXPECT_CALL(*mock_memory_controller_, SetStalePageAge(_))
      .WillRepeatedly(Return(Status::OK));
  EXPECT_OK(handler_->Update(spec, Container::UPDATE_REPLACE));
}

TEST_F(MemoryResourceUpdateReplaceTest, WithStalePageAgeFails) {
  ContainerSpec spec;
  spec.mutable_memory()->set_stale_page_age(42);
  EXPECT_CALL(*mock_memory_controller_, SetStalePageAge(_))
      .WillRepeatedly(Return(Status::CANCELLED));
  EXPECT_EQ(Status::CANCELLED,
            handler_->Update(spec, Container::UPDATE_REPLACE));
}

TEST_F(MemoryResourceUpdateReplaceTest, WithStalePageAgeNotSupported) {
  ContainerSpec spec;
  EXPECT_CALL(*mock_memory_controller_, SetStalePageAge(_))
      .WillRepeatedly(Return(Status(::util::error::NOT_FOUND, "")));
  EXPECT_OK(handler_->Update(spec, Container::UPDATE_REPLACE));
}

TEST_F(MemoryResourceUpdateReplaceTest, WithDirtyRatios) {
  ContainerSpec spec;
  spec.mutable_memory()->mutable_dirty()->set_ratio(42);
  spec.mutable_memory()->mutable_dirty()->set_background_ratio(43);
  EXPECT_CALL(*mock_memory_controller_, SetDirtyRatio(42))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_memory_controller_, SetDirtyBackgroundRatio(43))
      .WillOnce(Return(Status::OK));
  EXPECT_OK(handler_->Update(spec, Container::UPDATE_REPLACE));
}

TEST_F(MemoryResourceUpdateReplaceTest, WithDirtyLimits) {
  ContainerSpec spec;
  spec.mutable_memory()->mutable_dirty()->set_limit(42);
  spec.mutable_memory()->mutable_dirty()->set_background_limit(43);
  EXPECT_CALL(*mock_memory_controller_, SetDirtyRatio(_)).Times(0);
  EXPECT_CALL(*mock_memory_controller_, SetDirtyLimit(Bytes(42)))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_memory_controller_, SetDirtyBackgroundRatio(_)).Times(0);
  EXPECT_CALL(*mock_memory_controller_, SetDirtyBackgroundLimit(Bytes(43)))
      .WillOnce(Return(Status::OK));
  EXPECT_OK(handler_->Update(spec, Container::UPDATE_REPLACE));
}

TEST_F(MemoryResourceUpdateReplaceTest, MissingDirtyBGRatio) {
  ContainerSpec spec;
  spec.mutable_memory()->mutable_dirty()->set_ratio(42);
  EXPECT_CALL(*mock_memory_controller_, SetDirtyRatio(42))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_memory_controller_, SetDirtyLimit(_)).Times(0);
  EXPECT_CALL(*mock_memory_controller_, SetDirtyBackgroundRatio(_))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_memory_controller_, SetDirtyBackgroundLimit(_)).Times(0);
  EXPECT_OK(handler_->Update(spec, Container::UPDATE_REPLACE));
}

TEST_F(MemoryResourceUpdateReplaceTest, MissingDirtyRatio) {
  ContainerSpec spec;
  spec.mutable_memory()->mutable_dirty()->set_background_ratio(42);
  EXPECT_CALL(*mock_memory_controller_, SetDirtyRatio(_))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_memory_controller_, SetDirtyLimit(_)).Times(0);
  EXPECT_CALL(*mock_memory_controller_, SetDirtyBackgroundRatio(42))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_memory_controller_, SetDirtyBackgroundLimit(_)).Times(0);
  EXPECT_OK(handler_->Update(spec, Container::UPDATE_REPLACE));
}

TEST_F(MemoryResourceUpdateReplaceTest, MissingDirtyBGLimit) {
  ContainerSpec spec;
  spec.mutable_memory()->mutable_dirty()->set_limit(42);
  EXPECT_CALL(*mock_memory_controller_, SetDirtyRatio(_)).Times(0);
  EXPECT_CALL(*mock_memory_controller_, SetDirtyLimit(Bytes(42)))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_memory_controller_, SetDirtyBackgroundRatio(_)).Times(0);
  EXPECT_CALL(*mock_memory_controller_, SetDirtyBackgroundLimit(_))
      .WillOnce(Return(Status::OK));
  EXPECT_OK(handler_->Update(spec, Container::UPDATE_REPLACE));
}

TEST_F(MemoryResourceUpdateReplaceTest, MissingDirtyLimit) {
  ContainerSpec spec;
  spec.mutable_memory()->mutable_dirty()->set_background_limit(42);
  EXPECT_CALL(*mock_memory_controller_, SetDirtyRatio(_)).Times(0);
  EXPECT_CALL(*mock_memory_controller_, SetDirtyLimit(_))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_memory_controller_, SetDirtyBackgroundRatio(_)).Times(0);
  EXPECT_CALL(*mock_memory_controller_, SetDirtyBackgroundLimit(Bytes(42)))
      .WillOnce(Return(Status::OK));
  EXPECT_OK(handler_->Update(spec, Container::UPDATE_REPLACE));
}

TEST_F(MemoryResourceUpdateReplaceTest, InvalidDirtyValues) {
  ContainerSpec spec;
  spec.mutable_memory()->mutable_dirty()->set_ratio(42);
  spec.mutable_memory()->mutable_dirty()->set_background_ratio(43);
  spec.mutable_memory()->mutable_dirty()->set_limit(44);
  spec.mutable_memory()->mutable_dirty()->set_background_limit(45);
  EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT,
                    handler_->Update(spec, Container::UPDATE_REPLACE));
}

TEST_F(MemoryResourceUpdateReplaceTest, WithDirtyNothing) {
  ContainerSpec spec;
  EXPECT_CALL(*mock_memory_controller_, SetDirtyRatio(75))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_memory_controller_, SetDirtyBackgroundRatio(10))
      .WillOnce(Return(Status::OK));
  EXPECT_OK(handler_->Update(spec, Container::UPDATE_REPLACE));
}

TEST_F(MemoryResourceUpdateReplaceTest, WithDirtyNothingNotFound) {
  ContainerSpec spec;
  EXPECT_CALL(*mock_memory_controller_, SetDirtyRatio(75))
      .WillOnce(Return(Status(NOT_FOUND, "")));
  EXPECT_CALL(*mock_memory_controller_, SetDirtyBackgroundRatio(10))
      .WillOnce(Return(Status(NOT_FOUND, "")));
  EXPECT_OK(handler_->Update(spec, Container::UPDATE_REPLACE));
}

TEST_F(MemoryResourceUpdateReplaceTest, WithKMemChargeUsageSucceeds) {
  ContainerSpec spec;
  spec.mutable_memory()->set_kmem_charge_usage(true);
  EXPECT_CALL(*mock_memory_controller_, SetKMemChargeUsage(true))
      .WillOnce(Return(Status::OK));
  EXPECT_OK(handler_->Update(spec, Container::UPDATE_REPLACE));
}

TEST_F(MemoryResourceUpdateReplaceTest, WithKMemChargeUsageFails) {
  ContainerSpec spec;
  spec.mutable_memory()->set_kmem_charge_usage(true);
  EXPECT_CALL(*mock_memory_controller_, SetKMemChargeUsage(true))
      .WillOnce(Return(Status(NOT_FOUND, "")));
  EXPECT_NOT_OK(handler_->Update(spec, Container::UPDATE_REPLACE));
}

// Tests for RegisterNotification().

// Dummy callback used for testing.
void NoOpCallback(Status status) {
  EXPECT_TRUE(false) << "Should never be called";
}

TEST_F(MemoryResourceHandlerTest, RegisterNotificationOomSuccess) {
  EventSpec spec;
  spec.mutable_oom();

  // RegisterOomNotification takes ownership, but we're mocking it out.
  unique_ptr<Callback1<Status>> cb(NewPermanentCallback(&NoOpCallback));

  EXPECT_CALL(*mock_memory_controller_, RegisterOomNotification(NotNull()))
      .WillOnce(Return(1));

  StatusOr<ActiveNotifications::Handle> statusor =
      handler_->RegisterNotification(spec, cb.get());
  ASSERT_OK(statusor);
  EXPECT_EQ(1, statusor.ValueOrDie());
}

TEST_F(MemoryResourceHandlerTest, RegisterNotificationOomFails) {
  EventSpec spec;
  spec.mutable_oom();

  // RegisterOomNotification takes ownership, but we're mocking it out.
  unique_ptr<Callback1<Status>> cb(NewPermanentCallback(&NoOpCallback));

  EXPECT_CALL(*mock_memory_controller_, RegisterOomNotification(NotNull()))
      .WillOnce(Return(Status::CANCELLED));

  EXPECT_ERROR_CODE(::util::error::CANCELLED,
                    handler_->RegisterNotification(spec, cb.get()));
}

TEST_F(MemoryResourceHandlerTest, RegisterNotificationMemoryThresholdSuccess) {
  EventSpec spec;
  spec.mutable_memory_threshold()->set_usage(4096);

  // RegisterOomNotification takes ownership, but we're mocking it out.
  unique_ptr<Callback1<Status>> cb(NewPermanentCallback(&NoOpCallback));

  EXPECT_CALL(*mock_memory_controller_,
              RegisterUsageThresholdNotification(Bytes(4096), NotNull()))
      .WillOnce(Return(1));

  StatusOr<ActiveNotifications::Handle> statusor =
      handler_->RegisterNotification(spec, cb.get());
  ASSERT_OK(statusor);
  EXPECT_EQ(1, statusor.ValueOrDie());
}

TEST_F(MemoryResourceHandlerTest, RegisterNotificationMemoryThresholdFails) {
  EventSpec spec;
  spec.mutable_memory_threshold()->set_usage(4096);

  // RegisterOomNotification takes ownership, but we're mocking it out.
  unique_ptr<Callback1<Status>> cb(NewPermanentCallback(&NoOpCallback));

  EXPECT_CALL(*mock_memory_controller_,
              RegisterUsageThresholdNotification(Bytes(4096), NotNull()))
      .WillOnce(Return(Status::CANCELLED));

  EXPECT_ERROR_CODE(::util::error::CANCELLED,
                    handler_->RegisterNotification(spec, cb.get()));
}

TEST_F(MemoryResourceHandlerTest,
       RegisterNotificationMemoryThresholdNoThreshold) {
  EventSpec spec;
  spec.mutable_memory_threshold();

  EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT,
                    handler_->RegisterNotification(
                        spec, NewPermanentCallback(&NoOpCallback)));
}

TEST_F(MemoryResourceHandlerTest, RegisterNotificationNoneSpecified) {
  EventSpec spec;

  EXPECT_ERROR_CODE(::util::error::NOT_FOUND,
                    handler_->RegisterNotification(
                        spec, NewPermanentCallback(&NoOpCallback)));
}

TEST_F(MemoryResourceHandlerTest, RegisterNotificationMultipleSpecified) {
  EventSpec spec;
  spec.mutable_oom();
  spec.mutable_memory_threshold()->set_usage(4096);

  EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT,
                    handler_->RegisterNotification(
                        spec, NewPermanentCallback(&NoOpCallback)));
}

class MemorySpecGettingTest : public MemoryResourceHandlerTest {
 public:
  virtual void SetUp() {
    MemoryResourceHandlerTest::SetUp();
    Bytes kEmpty(0);
    EXPECT_CALL(*mock_memory_controller_, GetLimit())
        .WillRepeatedly(Return(StatusOr<Bytes>(kEmpty)));
    EXPECT_CALL(*mock_memory_controller_, GetSoftLimit())
        .WillRepeatedly(Return(StatusOr<Bytes>(kEmpty)));
    EXPECT_CALL(*mock_memory_controller_, GetOomScore())
        .WillRepeatedly(Return(0));
    EXPECT_CALL(*mock_memory_controller_, GetCompressionSamplingRatio())
        .WillRepeatedly(Return(0));
    EXPECT_CALL(*mock_memory_controller_, GetStalePageAge())
        .WillRepeatedly(Return(0));
    EXPECT_CALL(*mock_memory_controller_, GetDirtyRatio())
        .WillRepeatedly(Return(0));
    EXPECT_CALL(*mock_memory_controller_, GetDirtyBackgroundRatio())
        .WillRepeatedly(Return(0));
    EXPECT_CALL(*mock_memory_controller_, GetDirtyLimit())
        .WillRepeatedly(Return(StatusOr<Bytes>(Bytes(0))));
    EXPECT_CALL(*mock_memory_controller_, GetDirtyBackgroundLimit())
        .WillRepeatedly(Return(StatusOr<Bytes>(Bytes(0))));
    EXPECT_CALL(*mock_memory_controller_, GetKMemChargeUsage())
        .WillRepeatedly(Return(true));
  }
};

TEST_F(MemorySpecGettingTest, GetEvictionPriority) {
  ContainerSpec spec;
  const int32 kPriority = 123;
  EXPECT_CALL(*mock_memory_controller_, GetOomScore())
      .WillRepeatedly(Return(kPriority));
  EXPECT_OK(handler_->Spec(&spec));
  EXPECT_EQ(kPriority, spec.memory().eviction_priority());
}

TEST_F(MemorySpecGettingTest, GetEvictionPriorityFailed) {
  ContainerSpec spec;
  EXPECT_CALL(*mock_memory_controller_, GetOomScore())
      .WillRepeatedly(Return(::util::Status::CANCELLED));
  EXPECT_NOT_OK(handler_->Spec(&spec));
}

TEST_F(MemorySpecGettingTest, GetEvictionPriorityNotFound) {
  ContainerSpec spec;
  EXPECT_CALL(*mock_memory_controller_, GetOomScore())
      .WillOnce(Return(Status(::util::error::NOT_FOUND, "")));
  EXPECT_OK(handler_->Spec(&spec));
}


TEST_F(MemorySpecGettingTest, GetLimit) {
  ContainerSpec spec;
  Bytes limit(123);
  EXPECT_CALL(*mock_memory_controller_, GetLimit())
      .WillRepeatedly(Return(StatusOr<Bytes>(limit)));
  EXPECT_OK(handler_->Spec(&spec));
  EXPECT_EQ(123, spec.memory().limit());
}

TEST_F(MemorySpecGettingTest, GetLimitFailed) {
  ContainerSpec spec;
  EXPECT_CALL(*mock_memory_controller_, GetLimit())
      .WillOnce(Return(::util::Status::CANCELLED));
  EXPECT_NOT_OK(handler_->Spec(&spec));
}

TEST_F(MemorySpecGettingTest, GetReservation) {
  ContainerSpec spec;
  Bytes limit(12345);
  EXPECT_CALL(*mock_memory_controller_, GetSoftLimit())
      .WillRepeatedly(Return(StatusOr<Bytes>(limit)));
  EXPECT_OK(handler_->Spec(&spec));
  EXPECT_EQ(12345, spec.memory().reservation());
}

TEST_F(MemorySpecGettingTest, GetReservationFailed) {
  ContainerSpec spec;
  EXPECT_CALL(*mock_memory_controller_, GetSoftLimit())
      .WillOnce(Return(::util::Status::CANCELLED));
  EXPECT_NOT_OK(handler_->Spec(&spec));
}

TEST_F(MemorySpecGettingTest, GetCompressionSamplingRatio) {
  ContainerSpec spec;
  EXPECT_CALL(*mock_memory_controller_, GetCompressionSamplingRatio())
      .WillOnce(Return(StatusOr<int32>(42)));
  EXPECT_OK(handler_->Spec(&spec));
  EXPECT_EQ(42, spec.memory().compression_sampling_ratio());
}

TEST_F(MemorySpecGettingTest, GetCompressionSamplingRatioNotFound) {
  ContainerSpec spec;
  EXPECT_CALL(*mock_memory_controller_, GetCompressionSamplingRatio())
      .WillOnce(Return(Status(::util::error::NOT_FOUND, "")));
  EXPECT_OK(handler_->Spec(&spec));
  EXPECT_FALSE(spec.memory().has_compression_sampling_ratio());
}

TEST_F(MemorySpecGettingTest, GetCompressionSamplingRatioFailed) {
  ContainerSpec spec;
  EXPECT_CALL(*mock_memory_controller_, GetCompressionSamplingRatio())
      .WillOnce(Return(::util::Status::CANCELLED));
  EXPECT_NOT_OK(handler_->Spec(&spec));
}

TEST_F(MemorySpecGettingTest, GetStalePageAge) {
  ContainerSpec spec;
  EXPECT_CALL(*mock_memory_controller_, GetStalePageAge())
      .WillOnce(Return(StatusOr<int32>(42)));
  EXPECT_OK(handler_->Spec(&spec));
  EXPECT_EQ(42, spec.memory().stale_page_age());
}

TEST_F(MemorySpecGettingTest, GetStalePageAgeFailed) {
  ContainerSpec spec;
  EXPECT_CALL(*mock_memory_controller_, GetStalePageAge())
      .WillOnce(Return(::util::Status::CANCELLED));
  EXPECT_NOT_OK(handler_->Spec(&spec));
}

TEST_F(MemorySpecGettingTest, GetDirtyReturnsRatioOrLimit1) {
  ContainerSpec spec;
  EXPECT_CALL(*mock_memory_controller_, GetDirtyRatio())
      .WillOnce(Return(StatusOr<int32>(60)));
  EXPECT_CALL(*mock_memory_controller_, GetDirtyBackgroundRatio())
      .WillOnce(Return(StatusOr<int32>(0)));
  EXPECT_CALL(*mock_memory_controller_, GetDirtyBackgroundLimit())
      .WillOnce(Return(StatusOr<Bytes>(Bytes(10))));

  EXPECT_OK(handler_->Spec(&spec));
  EXPECT_FALSE(spec.memory().dirty().has_limit());
  EXPECT_FALSE(spec.memory().dirty().has_background_ratio());
  EXPECT_EQ(60, spec.memory().dirty().ratio());
  EXPECT_EQ(10, spec.memory().dirty().background_limit());
}

TEST_F(MemorySpecGettingTest, GetDirtyReturnsRatioOrLimit2) {
  ContainerSpec spec;
  EXPECT_CALL(*mock_memory_controller_, GetDirtyBackgroundRatio())
      .WillOnce(Return(StatusOr<int32>(20)));

  EXPECT_CALL(*mock_memory_controller_, GetDirtyLimit())
      .WillOnce(Return(StatusOr<Bytes>(Bytes(10))));

  EXPECT_OK(handler_->Spec(&spec));
  EXPECT_EQ(10, spec.memory().dirty().limit());
  EXPECT_EQ(20, spec.memory().dirty().background_ratio());
  EXPECT_FALSE(spec.memory().dirty().has_ratio());
  EXPECT_FALSE(spec.memory().dirty().has_background_limit());
}

TEST_F(MemorySpecGettingTest, GetDirtyReturnsRatioByDefault) {
  ContainerSpec spec;
  EXPECT_CALL(*mock_memory_controller_, GetDirtyRatio())
      .WillOnce(Return(StatusOr<int32>(0)));
  EXPECT_CALL(*mock_memory_controller_, GetDirtyBackgroundRatio())
      .WillOnce(Return(StatusOr<int32>(0)));

  EXPECT_CALL(*mock_memory_controller_, GetDirtyLimit())
      .WillOnce(Return(StatusOr<Bytes>(Bytes(0))));
  EXPECT_CALL(*mock_memory_controller_, GetDirtyBackgroundLimit())
      .WillOnce(Return(StatusOr<Bytes>(Bytes(0))));

  EXPECT_OK(handler_->Spec(&spec));
  EXPECT_FALSE(spec.memory().dirty().has_limit());
  EXPECT_FALSE(spec.memory().dirty().has_background_limit());
  EXPECT_EQ(0, spec.memory().dirty().ratio());
  EXPECT_EQ(0, spec.memory().dirty().background_ratio());
}

TEST_F(MemorySpecGettingTest, GetDirtyRatio) {
  ContainerSpec spec;
  EXPECT_CALL(*mock_memory_controller_, GetDirtyRatio())
      .WillOnce(Return(StatusOr<int32>(42)));
  EXPECT_OK(handler_->Spec(&spec));
  EXPECT_EQ(42, spec.memory().dirty().ratio());
}

TEST_F(MemorySpecGettingTest, GetDirtyRatioFailed) {
  ContainerSpec spec;
  EXPECT_CALL(*mock_memory_controller_, GetDirtyRatio())
      .WillOnce(Return(::util::Status::CANCELLED));
  EXPECT_NOT_OK(handler_->Spec(&spec));
}

TEST_F(MemorySpecGettingTest, GetDirtyBackgroundRatio) {
  ContainerSpec spec;
  EXPECT_CALL(*mock_memory_controller_, GetDirtyBackgroundRatio())
      .WillOnce(Return(StatusOr<int32>(42)));
  EXPECT_OK(handler_->Spec(&spec));
  EXPECT_EQ(42, spec.memory().dirty().background_ratio());
}

TEST_F(MemorySpecGettingTest, GetDirtyBackgroundRatioFailed) {
  ContainerSpec spec;
  EXPECT_CALL(*mock_memory_controller_, GetDirtyBackgroundRatio())
      .WillOnce(Return(::util::Status::CANCELLED));
  EXPECT_NOT_OK(handler_->Spec(&spec));
}

TEST_F(MemorySpecGettingTest, GetDirtyLimit) {
  ContainerSpec spec;
  EXPECT_CALL(*mock_memory_controller_, GetDirtyLimit())
      .WillOnce(Return(StatusOr<Bytes>(Bytes(42))));
  EXPECT_OK(handler_->Spec(&spec));
  EXPECT_EQ(42, spec.memory().dirty().limit());
}

TEST_F(MemorySpecGettingTest, GetDirtyLimitFailed) {
  ContainerSpec spec;
  EXPECT_CALL(*mock_memory_controller_, GetDirtyLimit())
      .WillOnce(Return(::util::Status::CANCELLED));
  EXPECT_NOT_OK(handler_->Spec(&spec));
}

TEST_F(MemorySpecGettingTest, GetDirtyBackgroundLimit) {
  ContainerSpec spec;
  EXPECT_CALL(*mock_memory_controller_, GetDirtyBackgroundLimit())
      .WillOnce(Return(StatusOr<Bytes>(Bytes(42))));
  EXPECT_OK(handler_->Spec(&spec));
  EXPECT_EQ(42, spec.memory().dirty().background_limit());
}

TEST_F(MemorySpecGettingTest, GetDirtyBackgroundLimitFailed) {
  ContainerSpec spec;
  EXPECT_CALL(*mock_memory_controller_, GetDirtyBackgroundLimit())
      .WillOnce(Return(::util::Status::CANCELLED));
  EXPECT_NOT_OK(handler_->Spec(&spec));
}

TEST_F(MemorySpecGettingTest, GetKMemChargeUsage) {
  ContainerSpec spec;
  EXPECT_CALL(*mock_memory_controller_, GetKMemChargeUsage())
      .WillOnce(Return(true));
  EXPECT_OK(handler_->Spec(&spec));
  EXPECT_TRUE(spec.memory().kmem_charge_usage());
}

TEST_F(MemorySpecGettingTest, GetKMemChargeUsageFailed) {
  ContainerSpec spec;
  EXPECT_CALL(*mock_memory_controller_, GetKMemChargeUsage())
      .WillOnce(Return(::util::Status::CANCELLED));
  EXPECT_NOT_OK(handler_->Spec(&spec));
}

}  // namespace lmctfy
}  // namespace containers
