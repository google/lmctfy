#ifndef LMCTFY__CLMCTFY_MACROS_CTEST_H_
#define LMCTFY__CLMCTFY_MACROS_CTEST_H_

// Some useful macros for writing test cases.

#define WITH_NULL_CONTAINER_API_RUN(func, ...) do { \
  ContainerApi *tmp = container_api_->container_api_; \
  container_api_->container_api_ = NULL;  \
  struct status s = {0, NULL};  \
  int ret = func(__VA_ARGS__, &s);  \
  EXPECT_EQ(ret, s.error_code); \
  EXPECT_EQ(s.error_code, UTIL__ERROR__CODE__INVALID_ARGUMENT); \
  container_api_->container_api_ = tmp; \
  \
  struct container_api *tmp_api = container_api_; \
  container_api_ = NULL;  \
  s = {0, NULL};  \
  ret = func(__VA_ARGS__, &s);  \
  EXPECT_EQ(ret, s.error_code); \
  EXPECT_EQ(s.error_code, UTIL__ERROR__CODE__INVALID_ARGUMENT); \
  container_api_ = tmp_api; \
} while(0)

#define WITH_NULL_CONTAINER_RUN(func, ...) do { \
  Container *tmp = container_->container_;  \
  container_->container_ = NULL;  \
  struct status s = {0, NULL};  \
  int ret = func(__VA_ARGS__, &s);  \
  EXPECT_EQ(ret, s.error_code); \
  EXPECT_EQ(s.error_code, UTIL__ERROR__CODE__INVALID_ARGUMENT); \
  container_->container_ = tmp; \
  \
  struct container *tmp_container = container_; \
  container_ = NULL;  \
  s = {0, NULL};  \
  ret = func(__VA_ARGS__, &s);  \
  EXPECT_EQ(ret, s.error_code); \
  EXPECT_EQ(s.error_code, UTIL__ERROR__CODE__INVALID_ARGUMENT); \
  container_ = tmp_container; \
} while (0)

#define SHOULD_SUCCEED(func, ...) do { \
  struct status s = {0, NULL};  \
  int ret = func(__VA_ARGS__, &s);  \
  EXPECT_EQ(ret, 0);  \
  EXPECT_EQ(s.error_code, 0); \
  EXPECT_EQ(s.message, NULL); \
} while (0)

#define SHOULD_FAIL_WITH_ERROR(st, func, ...) do {  \
  struct status s = {0, NULL};  \
  int ret = func(__VA_ARGS__, &s);  \
  EXPECT_EQ(ret, s.error_code); \
  EXPECT_EQ(s.error_code, (st).error_code()); \
  EXPECT_EQ((st).error_message(), s.message); \
  free(s.message);  \
} while (0)

#define SHOULD_BE_INVALID_ARGUMENT(func, ...) do {  \
  struct status s = {0, NULL};  \
  int ret = func(__VA_ARGS__, &s);  \
  EXPECT_EQ(ret, s.error_code); \
  EXPECT_EQ(s.error_code, UTIL__ERROR__CODE__INVALID_ARGUMENT); \
  if (s.message != NULL) {  \
    free(s.message);  \
  } \
} while (0)

#endif // LMCTFY__CLMCTFY_MACROS_CTEST_H_

