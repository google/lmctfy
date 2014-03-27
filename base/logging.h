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

#ifndef BASE_LOGGING_H__
#define BASE_LOGGING_H__

#include <string>

#include "gflags/gflags.h"

// Log messages at a level >= this flag are sent to stderr.
DECLARE_int32(stderrthreshold);

// Log suppression level: messages logged at a lower level than this are
// suppressed.
DECLARE_int32(minloglevel);

enum LogLevel {
  LOGLEVEL_INFO,     // Informational.
  LOGLEVEL_WARNING,  // Warns about issues that, although not technically a
                     // problem now, could cause problems in the future.  For
                     // example, a // warning will be printed when parsing a
                     // message that is near the message size limit.
  LOGLEVEL_ERROR,    // An error occurred which should never happen during
                     // normal use.
  LOGLEVEL_FATAL,    // An error occurred from which the library cannot
                     // recover.  This usually indicates a programming error
                     // in the code which calls the library, especially when
                     // compiled in debug mode.

#ifdef NDEBUG
  LOGLEVEL_DFATAL = LOGLEVEL_ERROR
#else
  LOGLEVEL_DFATAL = LOGLEVEL_FATAL
#endif
};

#ifdef NDEBUG
const bool DEBUG_MODE = false;
#else
const bool DEBUG_MODE = true;
#endif

namespace internal {

class LogFinisher;

class LogMessage {
 public:
  LogMessage(LogLevel level, const char* filename, int line);
  ~LogMessage();

  LogMessage& operator<<(const std::string& value);
  LogMessage& operator<<(const char* value);
  LogMessage& operator<<(char value);
  LogMessage& operator<<(int value);
  LogMessage& operator<<(uint value);
  LogMessage& operator<<(long value);
  LogMessage& operator<<(unsigned long value);
  LogMessage& operator<<(long long value);
  LogMessage& operator<<(unsigned long long value);
  LogMessage& operator<<(double value);

 private:
  friend class LogFinisher;
  void Finish();

  LogLevel level_;
  const char* filename_;
  int line_;
  std::string message_;
};

// Used to make the entire "LOG(BLAH) << etc." expression have a void return
// type and print a newline after each message.
class LogFinisher {
 public:
  void operator=(LogMessage& other);
};

}  // namespace internal

// Undef everything in case we're being mixed with some other Google library
// which already defined them itself.  Presumably all Google libraries will
// support the same syntax for these so it should not be a big deal if they
// end up using our definitions instead.
#undef LOG
#undef LOG_IF

#undef CHECK
#undef CHECK_EQ
#undef CHECK_NE
#undef CHECK_LT
#undef CHECK_LE
#undef CHECK_GT
#undef CHECK_GE
#undef CHECK_NOTNULL

#undef DLOG
#undef DCHECK
#undef DCHECK_EQ
#undef DCHECK_NE
#undef DCHECK_LT
#undef DCHECK_LE
#undef DCHECK_GT
#undef DCHECK_GE

#define LOG(LEVEL)            \
  ::internal::LogFinisher() = \
      ::internal::LogMessage(::LOGLEVEL_##LEVEL, __FILE__, __LINE__)
#define LOG_IF(LEVEL, CONDITION) \
  !(CONDITION) ? (void)0 : LOG(LEVEL)

#define CHECK(EXPRESSION) \
  LOG_IF(FATAL, !(EXPRESSION)) << "CHECK failed: " #EXPRESSION ": "
#define CHECK_EQ(A, B) CHECK((A) == (B))
#define CHECK_NE(A, B) CHECK((A) != (B))
#define CHECK_LT(A, B) CHECK((A) <  (B))
#define CHECK_LE(A, B) CHECK((A) <= (B))
#define CHECK_GT(A, B) CHECK((A) >  (B))
#define CHECK_GE(A, B) CHECK((A) >= (B))

namespace internal {
template<typename T>
T* CheckNotNull(const char *file, int line, const char *name, T* val) {
  if (val == NULL) {
    LOG(FATAL) << name;
  }
  return val;
}
}  // namespace internal
#define CHECK_NOTNULL(A) \
  internal::CheckNotNull(__FILE__, __LINE__, "'" #A "' Must be non NULL", (A))

#ifdef NDEBUG

#define DLOG LOG_IF(INFO, false)

#define DCHECK(EXPRESSION) while(false) CHECK(EXPRESSION)
#define DCHECK_EQ(A, B) DCHECK((A) == (B))
#define DCHECK_NE(A, B) DCHECK((A) != (B))
#define DCHECK_LT(A, B) DCHECK((A) <  (B))
#define DCHECK_LE(A, B) DCHECK((A) <= (B))
#define DCHECK_GT(A, B) DCHECK((A) >  (B))
#define DCHECK_GE(A, B) DCHECK((A) >= (B))

#else  // NDEBUG

#define DLOG LOG

#define DCHECK    CHECK
#define DCHECK_EQ CHECK_EQ
#define DCHECK_NE CHECK_NE
#define DCHECK_LT CHECK_LT
#define DCHECK_LE CHECK_LE
#define DCHECK_GT CHECK_GT
#define DCHECK_GE CHECK_GE

#endif  // !NDEBUG

typedef void LogHandler(LogLevel level, const char* filename, int line,
                        const std::string& message);

// The library sometimes writes warning and error messages to These messages are
// primarily useful for developers, but may also help end users figure out a
// problem.  If you would prefer that these messages be sent somewhere other
// than stderr, call SetLogHandler() to set your own handler.  This returns the
// old handler.  Set the handler to NULL to ignore log messages (but see also
// LogSilencer, below).
//
// Obviously, SetLogHandler is not thread-safe.  You should only call it
// at initialization time, and probably not from library code.  If you
// simply want to suppress log messages temporarily (e.g. because you
// have some code that tends to trigger them frequently and you know
// the warnings are not important to you), use the LogSilencer class
// below.
LogHandler* SetLogHandler(LogHandler* new_func);

// Create a LogSilencer if you want to temporarily suppress all log
// messages.  As long as any LogSilencer objects exist, non-fatal
// log messages will be discarded (the current LogHandler will *not*
// be called).  Constructing a LogSilencer is thread-safe.  You may
// accidentally suppress log messages occurring in another thread, but
// since messages are generally for debugging purposes only, this isn't
// a big deal.  If you want to intercept log messages, use SetLogHandler().
class LogSilencer {
 public:
  LogSilencer();
  ~LogSilencer();
};

// A thread-safe replacement for strerror(). Returns a string describing the
// given POSIX error code.
::std::string StrError(int err);

#endif  // BASE_LOGGING_H__
