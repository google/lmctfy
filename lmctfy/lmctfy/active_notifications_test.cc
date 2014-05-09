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

#include "lmctfy/active_notifications.h"

#include "gtest/gtest.h"

namespace containers {
namespace lmctfy {
namespace {

TEST(ActiveNotificationsTest, Add) {
  ActiveNotifications active_notifications;

  EXPECT_LT(0, active_notifications.Add());
  EXPECT_LT(0, active_notifications.Add());
  EXPECT_LT(0, active_notifications.Add());
  EXPECT_EQ(3, active_notifications.Size());
}

TEST(ActiveNotificationsTest, Remove) {
  ActiveNotifications active_notifications;

  ActiveNotifications::Handle id1 = active_notifications.Add();
  ActiveNotifications::Handle id2 = active_notifications.Add();
  ActiveNotifications::Handle id3 = active_notifications.Add();
  ASSERT_EQ(3, active_notifications.Size());

  EXPECT_TRUE(active_notifications.Remove(id1));
  EXPECT_EQ(2, active_notifications.Size());
  EXPECT_FALSE(active_notifications.Remove(id1));

  EXPECT_TRUE(active_notifications.Remove(id2));
  EXPECT_EQ(1, active_notifications.Size());
  EXPECT_FALSE(active_notifications.Remove(id2));

  EXPECT_TRUE(active_notifications.Remove(id3));
  EXPECT_EQ(0, active_notifications.Size());
  EXPECT_FALSE(active_notifications.Remove(id3));
}

TEST(ActiveNotificationsTest, Contains) {
  ActiveNotifications active_notifications;

  ASSERT_EQ(0, active_notifications.Size());
  EXPECT_FALSE(active_notifications.Contains(42));

  ActiveNotifications::Handle id1 = active_notifications.Add();
  ActiveNotifications::Handle id2 = active_notifications.Add();
  ActiveNotifications::Handle id3 = active_notifications.Add();
  ASSERT_EQ(3, active_notifications.Size());
  EXPECT_TRUE(active_notifications.Contains(id1));
  EXPECT_TRUE(active_notifications.Contains(id2));
  EXPECT_TRUE(active_notifications.Contains(id3));

  ASSERT_TRUE(active_notifications.Remove(id1));
  EXPECT_EQ(2, active_notifications.Size());
  EXPECT_FALSE(active_notifications.Contains(id1));

  ASSERT_TRUE(active_notifications.Remove(id2));
  EXPECT_EQ(1, active_notifications.Size());
  EXPECT_FALSE(active_notifications.Contains(id2));

  ASSERT_TRUE(active_notifications.Remove(id3));
  EXPECT_EQ(0, active_notifications.Size());
  EXPECT_FALSE(active_notifications.Contains(id3));
}

}  // namespace
}  // namespace lmctfy
}  // namespace containers
