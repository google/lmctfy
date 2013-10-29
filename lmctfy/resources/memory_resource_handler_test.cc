// Copyright 2013 Google Inc. All Rights Reserved.
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
#include "util/bytes.h"
#include "system_api/kernel_api_mock.h"
#include "lmctfy/controllers/cgroup_factory_mock.h"
#include "lmctfy/controllers/eventfd_notifications_mock.h"
#include "lmctfy/controllers/memory_controller_mock.h"
#include "lmctfy/resource_handler.h"
#include "include/lmctfy.pb.h"
#include "util/errors_test_util.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "util/task/codes.pb.h"
#include "util/task/status.h"

using ::util::Bytes;
using ::system_api::KernelAPIMock;
using ::std::unique_ptr;
using ::std::vector;
using ::testing::NiceMock;
using ::testing::NotNull;
using ::testing::Return;
using ::testing::StrictMock;
using ::testing::_;
using ::util::Status;
using ::util::StatusOr;

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
  }

 protected:
  const vector<Container::StatsType> kStatTypes;

  MockMemoryController *mock_memory_controller_;
  unique_ptr<KernelAPIMock> mock_kernel_;
  unique_ptr<MemoryResourceHandler> handler_;
};

// Tests for Stats()

TEST_F(MemoryResourceHandlerTest, StatsSuccess) {
  for (Container::StatsType type : kStatTypes) {
    ContainerStats stats;

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

    EXPECT_TRUE(handler_->Stats(type, &stats).ok());
    ASSERT_TRUE(stats.has_memory());
    EXPECT_EQ(1, stats.memory().working_set());
    EXPECT_EQ(2, stats.memory().usage());
    EXPECT_EQ(3, stats.memory().max_usage());
    EXPECT_EQ(4, stats.memory().limit());
    EXPECT_EQ(5, stats.memory().effective_limit());
    EXPECT_EQ(6, stats.memory().reservation());
  }
}

TEST_F(MemoryResourceHandlerTest, StatsGetWorkingSetFails) {
  for (Container::StatsType type : kStatTypes) {
    ContainerStats stats;

    EXPECT_CALL(*mock_memory_controller_, GetWorkingSet())
        .WillRepeatedly(Return(Status::CANCELLED));
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

    EXPECT_EQ(Status::CANCELLED, handler_->Stats(type, &stats));
  }
}

TEST_F(MemoryResourceHandlerTest, StatsGetUsage) {
  for (Container::StatsType type : kStatTypes) {
    ContainerStats stats;

    EXPECT_CALL(*mock_memory_controller_, GetWorkingSet())
        .WillRepeatedly(Return(Bytes(1)));
    EXPECT_CALL(*mock_memory_controller_, GetUsage())
        .WillRepeatedly(Return(Status::CANCELLED));
    EXPECT_CALL(*mock_memory_controller_, GetMaxUsage())
        .WillRepeatedly(Return(Bytes(3)));
    EXPECT_CALL(*mock_memory_controller_, GetLimit())
        .WillRepeatedly(Return(Bytes(4)));
    EXPECT_CALL(*mock_memory_controller_, GetEffectiveLimit())
        .WillRepeatedly(Return(Bytes(5)));
    EXPECT_CALL(*mock_memory_controller_, GetSoftLimit())
        .WillRepeatedly(Return(Bytes(6)));

    EXPECT_EQ(Status::CANCELLED, handler_->Stats(type, &stats));
  }
}

TEST_F(MemoryResourceHandlerTest, StatsGetMaxUsageFails) {
  for (Container::StatsType type : kStatTypes) {
    ContainerStats stats;

    EXPECT_CALL(*mock_memory_controller_, GetWorkingSet())
        .WillRepeatedly(Return(Bytes(1)));
    EXPECT_CALL(*mock_memory_controller_, GetUsage())
        .WillRepeatedly(Return(Bytes(2)));
    EXPECT_CALL(*mock_memory_controller_, GetMaxUsage())
        .WillRepeatedly(Return(Status::CANCELLED));
    EXPECT_CALL(*mock_memory_controller_, GetLimit())
        .WillRepeatedly(Return(Bytes(4)));
    EXPECT_CALL(*mock_memory_controller_, GetEffectiveLimit())
        .WillRepeatedly(Return(Bytes(5)));
    EXPECT_CALL(*mock_memory_controller_, GetSoftLimit())
        .WillRepeatedly(Return(Bytes(6)));

    EXPECT_EQ(Status::CANCELLED, handler_->Stats(type, &stats));
  }
}

TEST_F(MemoryResourceHandlerTest, StatsGetLimitFails) {
  for (Container::StatsType type : kStatTypes) {
    ContainerStats stats;

    EXPECT_CALL(*mock_memory_controller_, GetWorkingSet())
        .WillRepeatedly(Return(Bytes(1)));
    EXPECT_CALL(*mock_memory_controller_, GetUsage())
        .WillRepeatedly(Return(Bytes(2)));
    EXPECT_CALL(*mock_memory_controller_, GetMaxUsage())
        .WillRepeatedly(Return(Bytes(3)));
    EXPECT_CALL(*mock_memory_controller_, GetLimit())
        .WillRepeatedly(Return(Status::CANCELLED));
    EXPECT_CALL(*mock_memory_controller_, GetEffectiveLimit())
        .WillRepeatedly(Return(Bytes(5)));
    EXPECT_CALL(*mock_memory_controller_, GetSoftLimit())
        .WillRepeatedly(Return(Bytes(6)));

    EXPECT_EQ(Status::CANCELLED, handler_->Stats(type, &stats));
  }
}

TEST_F(MemoryResourceHandlerTest, StatsGetEffectiveLimitFails) {
  for (Container::StatsType type : kStatTypes) {
    ContainerStats stats;

    EXPECT_CALL(*mock_memory_controller_, GetWorkingSet())
        .WillRepeatedly(Return(Bytes(1)));
    EXPECT_CALL(*mock_memory_controller_, GetUsage())
        .WillRepeatedly(Return(Bytes(2)));
    EXPECT_CALL(*mock_memory_controller_, GetMaxUsage())
        .WillRepeatedly(Return(Bytes(3)));
    EXPECT_CALL(*mock_memory_controller_, GetLimit())
        .WillRepeatedly(Return(Bytes(4)));
    EXPECT_CALL(*mock_memory_controller_, GetEffectiveLimit())
        .WillRepeatedly(Return(Status::CANCELLED));
    EXPECT_CALL(*mock_memory_controller_, GetSoftLimit())
        .WillRepeatedly(Return(Bytes(6)));

    EXPECT_EQ(Status::CANCELLED, handler_->Stats(type, &stats));
  }
}

TEST_F(MemoryResourceHandlerTest, StatsGetSoftLimitFails) {
  for (Container::StatsType type : kStatTypes) {
    ContainerStats stats;

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

TEST_F(MemoryResourceHandlerTest, UpdateReplaceEmpty) {
  ContainerSpec spec;

  EXPECT_CALL(*mock_memory_controller_, SetLimit(Bytes(-1)))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_memory_controller_, SetSoftLimit(Bytes(0)))
      .WillOnce(Return(Status::OK));

  EXPECT_TRUE(handler_->Update(spec, Container::UPDATE_REPLACE).ok());
}

TEST_F(MemoryResourceHandlerTest, UpdateReplaceNoReservationSuccess) {
  ContainerSpec spec;
  spec.mutable_memory()->set_limit(42);

  EXPECT_CALL(*mock_memory_controller_, SetLimit(Bytes(42)))
      .WillOnce(Return(Status::OK));
  EXPECT_CALL(*mock_memory_controller_, SetSoftLimit(Bytes(0)))
      .WillOnce(Return(Status::OK));

  EXPECT_TRUE(handler_->Update(spec, Container::UPDATE_REPLACE).ok());
}

TEST_F(MemoryResourceHandlerTest, UpdateReplaceNoReservationLimitFails) {
  ContainerSpec spec;
  spec.mutable_memory()->set_limit(42);

  EXPECT_CALL(*mock_memory_controller_, SetLimit(Bytes(42)))
      .WillRepeatedly(Return(Status::CANCELLED));
  EXPECT_CALL(*mock_memory_controller_, SetSoftLimit(Bytes(0)))
      .WillRepeatedly(Return(Status::OK));

  EXPECT_EQ(Status::CANCELLED,
            handler_->Update(spec, Container::UPDATE_REPLACE));
}

TEST_F(MemoryResourceHandlerTest, UpdateReplaceNoReservationReservationFails) {
  ContainerSpec spec;
  spec.mutable_memory()->set_limit(42);

  EXPECT_CALL(*mock_memory_controller_, SetLimit(Bytes(42)))
      .WillRepeatedly(Return(Status::OK));
  EXPECT_CALL(*mock_memory_controller_, SetSoftLimit(Bytes(0)))
      .WillRepeatedly(Return(Status::CANCELLED));

  EXPECT_EQ(Status::CANCELLED,
            handler_->Update(spec, Container::UPDATE_REPLACE));
}

TEST_F(MemoryResourceHandlerTest, UpdateReplaceWithReservationSuccess) {
  ContainerSpec spec;
  spec.mutable_memory()->set_limit(42);
  spec.mutable_memory()->set_reservation(43);

  EXPECT_CALL(*mock_memory_controller_, SetLimit(Bytes(42)))
      .WillRepeatedly(Return(Status::OK));
  EXPECT_CALL(*mock_memory_controller_, SetSoftLimit(Bytes(43)))
      .WillRepeatedly(Return(Status::OK));

  EXPECT_TRUE(handler_->Update(spec, Container::UPDATE_REPLACE).ok());
}

TEST_F(MemoryResourceHandlerTest, UpdateReplaceWithReservationFails) {
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
    Bytes empty(0);
    EXPECT_CALL(*mock_memory_controller_, GetLimit())
        .WillRepeatedly(Return(StatusOr<Bytes>(empty)));
    EXPECT_CALL(*mock_memory_controller_, GetSoftLimit())
        .WillRepeatedly(Return(StatusOr<Bytes>(empty)));
  }
};

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

}  // namespace lmctfy
}  // namespace containers
