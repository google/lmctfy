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

#include "nscon/ns_handle.h"

#include <memory>

#include "util/errors_test_util.h"
#include "util/file_lines_test_util.h"
#include "strings/substitute.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::std::unique_ptr;
using ::testing::NotNull;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::StrEq;
using ::testing::_;
using ::util::Status;
using ::util::StatusOr;

namespace containers {
namespace nscon {

///////////////////////////////////////////////////////////////////////////////
// CookieGenerator class tests

class CookieGeneratorTest : public ::testing::Test {
 public:
  void SetUp() override {
    cg_.reset(new CookieGenerator());
  }

 protected:
  const pid_t kPid = 9999;
  const string kProcStatPath = "/proc/9999/stat";
  // Cookie format is:
  //  * character 'c'
  //  * start time
  // Value as obtained from ::strings::Substitute("c$0", start_time=3735928559).
  const string kCookieStr = "c3735928559";
  unique_ptr<CookieGenerator> cg_;
  ::util::FileLinesTestUtil mock_file_lines_;
};

static const char kProcStatContents[] =
    "5629 (cat) R 5510 5629 5510 34854 5629 4202496 221 0 0 0 0 0 0 0 20 0 1 0 "
    "3735928559 18407424 89 18446744073709551615 4194304 4237340 "
    "140735667877392 140735476648984 140171163455456 0 0 0 0 0 0 0 17 10 0 0 0 "
    "0 0";

TEST_F(CookieGeneratorTest, GenerateCookie) {
  mock_file_lines_.ExpectFileLines(kProcStatPath, {kProcStatContents});

  StatusOr<string> statusor = cg_->GenerateCookie(kPid);
  ASSERT_OK(statusor);
  EXPECT_EQ(kCookieStr, statusor.ValueOrDie());
}

TEST_F(CookieGeneratorTest, GenerateCookieNoProcStatContents) {
  mock_file_lines_.ExpectFileLines(kProcStatPath, {});
  EXPECT_ERROR_CODE(::util::error::INTERNAL, cg_->GenerateCookie(kPid));
}

TEST_F(CookieGeneratorTest, GenerateCookieInvalidProcStatContents) {
  mock_file_lines_.ExpectFileLines(kProcStatPath, {"1 2 3 4"});
  EXPECT_ERROR_CODE(::util::error::INTERNAL, cg_->GenerateCookie(kPid));
}

///////////////////////////////////////////////////////////////////////////////
// NsHandleFactory class tests

// MockCookieGenerator for further tests
class MockCookieGenerator : public CookieGenerator {
 public:
  MockCookieGenerator() {}

  MOCK_CONST_METHOD1(GenerateCookie, StatusOr<string>(pid_t pid));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockCookieGenerator);
};

class NsHandleFactoryTest : public ::testing::Test {
 public:
  void SetUp() override {
    mock_cg_ = new MockCookieGenerator();
    nsh_factory_.reset(new NsHandleFactory(mock_cg_));
  }

 protected:
  const pid_t kPid = 9999;
  const string kCookieStr = "c3735928559";
  // Handle (ToString()) format is:
  //  * cookie
  //  * character '-'
  //  * pid
  // Value as obtained from ::strings::Substitute("c$0-$1", kCookieStr, kPid).
  const string kHandleStr = "c3735928559-9999";
  MockCookieGenerator *mock_cg_;
  unique_ptr<NsHandleFactory> nsh_factory_;
};

TEST_F(NsHandleFactoryTest, GetWithPid) {
  EXPECT_CALL(*mock_cg_, GenerateCookie(kPid)).WillOnce(Return(kCookieStr));

  StatusOr<const NsHandle *> statusor = nsh_factory_->Get(kPid);
  EXPECT_OK(statusor);
  delete statusor.ValueOrDie();
}

TEST_F(NsHandleFactoryTest, GetWithInvalidPid) {
  EXPECT_CALL(*mock_cg_, GenerateCookie(kPid))
      .WillOnce(Return(Status(::util::error::INVALID_ARGUMENT, "Invalid Arg")));
  EXPECT_ERROR_CODE(::util::error::INVALID_ARGUMENT, nsh_factory_->Get(kPid));
}

TEST_F(NsHandleFactoryTest, GetWithHandlestr) {
  EXPECT_CALL(*mock_cg_, GenerateCookie(kPid)).WillOnce(Return(kCookieStr));

  StatusOr<const NsHandle *> statusor = nsh_factory_->Get(kHandleStr);
  EXPECT_OK(statusor);
  delete statusor.ValueOrDie();
}

TEST_F(NsHandleFactoryTest, GetWithMalformedHandlestr) {
  // By default, CookieGenerator will return success for all PIDs
  EXPECT_CALL(*mock_cg_, GenerateCookie(_)).WillRepeatedly(Return(kCookieStr));

  // Mark kPid as invalid process id.
  EXPECT_CALL(*mock_cg_, GenerateCookie(kPid))
      .WillOnce(Return(Status(::util::error::INVALID_ARGUMENT, "Invalid Arg")));

  EXPECT_NOT_OK(nsh_factory_->Get("abcd123"));
  EXPECT_NOT_OK(nsh_factory_->Get("123abcd"));
  EXPECT_NOT_OK(nsh_factory_->Get("cd123"));
  EXPECT_NOT_OK(nsh_factory_->Get("c123"));
  EXPECT_NOT_OK(nsh_factory_->Get("c123c123"));
  EXPECT_NOT_OK(nsh_factory_->Get("-123c123"));
  EXPECT_NOT_OK(nsh_factory_->Get("c-123"));
  EXPECT_NOT_OK(nsh_factory_->Get("c-1-23"));
  EXPECT_NOT_OK(nsh_factory_->Get("c--33"));
  EXPECT_NOT_OK(nsh_factory_->Get(kCookieStr));
  EXPECT_NOT_OK(nsh_factory_->Get("0" + kCookieStr + "-33"));
  EXPECT_NOT_OK(nsh_factory_->Get("x" + kCookieStr + "-33"));
  EXPECT_NOT_OK(nsh_factory_->Get(kCookieStr +  "00-33"));
  EXPECT_NOT_OK(nsh_factory_->Get(kCookieStr + "-2-3"));
  EXPECT_NOT_OK(nsh_factory_->Get(kCookieStr + "1.2-3"));
  EXPECT_NOT_OK(nsh_factory_->Get(kCookieStr + "-3.3"));
  EXPECT_NOT_OK(nsh_factory_->Get(kCookieStr + "-33-"));
  EXPECT_NOT_OK(nsh_factory_->Get(kCookieStr + ".-33-"));
  EXPECT_NOT_OK(nsh_factory_->Get(kCookieStr + "-33."));
  EXPECT_NOT_OK(nsh_factory_->Get(kCookieStr + "%s-33"));
  EXPECT_NOT_OK(nsh_factory_->Get(kCookieStr + "--33"));
  EXPECT_NOT_OK(nsh_factory_->Get(kCookieStr +  "-9999"));
}

///////////////////////////////////////////////////////////////////////////////
// NsHandle class tests

class NsHandleTest : public ::testing::Test {
 public:
  void SetUp() override {
    mock_cg_.reset(new MockCookieGenerator());
    nshandle_.reset(new NsHandle(kPid, kCookieStr, mock_cg_.get()));
  }

 protected:
  const pid_t kPid = 9999;
  const string kCookieStr = "c3735928559";
  const string kHandleStr = "c3735928559-9999";
  unique_ptr<MockCookieGenerator> mock_cg_;
  unique_ptr<NsHandle> nshandle_;
};

TEST_F(NsHandleTest, ToString) {
  EXPECT_EQ(kHandleStr, nshandle_->ToString());
}

TEST_F(NsHandleTest, ToPid) {
  EXPECT_EQ(kPid, nshandle_->ToPid());
}

TEST_F(NsHandleTest, IsValid) {
  // CookieGenerator will return success for kPid
  EXPECT_CALL(*mock_cg_, GenerateCookie(kPid)).WillOnce(Return(kCookieStr));
  EXPECT_TRUE(nshandle_->IsValid());

  // Mark kPid as invalid to simulate process death.
  EXPECT_CALL(*mock_cg_, GenerateCookie(kPid))
      .WillOnce(Return(Status(::util::error::INVALID_ARGUMENT, "Invalid Arg")));
  EXPECT_FALSE(nshandle_->IsValid());

  // Simulate PID-reuse by returning with a different cookie.
  EXPECT_CALL(*mock_cg_, GenerateCookie(kPid)).WillOnce(Return(kCookieStr+"1"));
  EXPECT_FALSE(nshandle_->IsValid());
}

}  // namespace nscon
}  // namespace containers
