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

// Useful helpers for testing code with ::util::Status and ::util::StatusOr<>.

#ifndef UTIL_ERRORS_TEST_UTIL_H_
#define UTIL_ERRORS_TEST_UTIL_H_

#include "gtest/gtest.h"
#include "util/errors.h"
#include "util/task/status.h"
#include "util/task/statusor.h"

// Test for "OK".  This works for a Status or a StatusOr.  These have to be
// wrapped in do-while because they evaluate a macro argument more than once.
// This precludes adding extra streamed information, which is unfortunate but
// not that common.  E.g.:
//    EXPECT_OK(status) << "oh no!";  // fails to compile
#define EXPECT_OK(expr)                                              \
    do {                                                             \
      auto _s = ::util::errors_internal::ToStatus(expr); \
      EXPECT_TRUE(_s.ok()) << _s;                                    \
    } while (0)
#define ASSERT_OK(expr)                                              \
    do {                                                             \
      auto _s = ::util::errors_internal::ToStatus(expr); \
      ASSERT_TRUE(_s.ok()) << _s;                                    \
    } while (0)
#define EXPECT_NOT_OK(expr) EXPECT_FALSE((expr).ok())
#define ASSERT_NOT_OK(expr) ASSERT_FALSE((expr).ok())

// Test for a specific error code.  This works for a Status or a StatusOr.
#define EXPECT_ERROR_CODE(code, expr) \
    EXPECT_EQ((code),                 \
        ::util::errors_internal::ToStatus(expr).error_code())
#define ASSERT_ERROR_CODE(code, expr) \
    ASSERT_EQ((code),                 \
        ::util::errors_internal::ToStatus(expr).error_code())

// Test for a specific error substring.  This works for a Status or a StatusOr.
#define EXPECT_ERROR_SUBSTR(substr, expr)                   \
    EXPECT_PRED_FORMAT2(::testing::IsSubstring, (substr),   \
        ::util::errors_internal::ToStatus(expr).error_message())
#define ASSERT_ERROR_SUBSTR(substr, expr)                   \
    ASSERT_PRED_FORMAT2(::testing::IsSubstring, (substr),   \
        ::util::errors_internal::ToStatus(expr).error_message())

// Test for a specific error code and error substring.  This works for a Status
// or a StatusOr.  These have to be wrapped in do-while because they evaluate a
// macro argument more than once.  This precludes adding extra streamed
// information, which is unfortunate but not that common.  See the example at
// EXPECT_OK().
#define EXPECT_ERROR_CODE_AND_SUBSTR(code, substr, expr)             \
    do {                                                             \
      auto _s = ::util::errors_internal::ToStatus(expr); \
      EXPECT_ERROR_CODE(code, _s);                                   \
      EXPECT_ERROR_SUBSTR(substr, _s);                               \
    } while (0)
#define ASSERT_ERROR_CODE_AND_SUBSTR(code, substr, expr)             \
    do {                                                             \
      auto _s = ::util::errors_internal::ToStatus(expr); \
      ASSERT_ERROR_CODE(code, _s);                                   \
      ASSERT_ERROR_SUBSTR(substr, _s);                               \
    } while (0)

#endif  // UTIL_ERRORS_TEST_UTIL_H_
