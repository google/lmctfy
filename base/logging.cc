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

#include "base/logging.h"

#include "base/mutex.h"

using ::std::string;

DEFINE_int32(stderrthreshold,
             LOGLEVEL_ERROR,
             "log messages at or above this level are copied to stderr");
DEFINE_int32(minloglevel,
             LOGLEVEL_INFO,
             "Messages logged at a lower level than this don't actually get "
             "logged anywhere");

namespace internal {

void DefaultLogHandler(LogLevel level, const char* filename, int line,
                       const string& message) {
  static const char* level_names[] = {"INFO", "WARNING", "ERROR", "FATAL"};

  // Only log errors above minloglevel.
  if (level >= FLAGS_minloglevel) {
    // We use fprintf() instead of cerr because we want this to work at static
    // initialization time.
    fprintf(stderr, "[lmctfy %s %s:%d] %s\n", level_names[level], filename,
            line, message.c_str());
    fflush(stderr);  // Needed on MSVC.
  }
}

void NullLogHandler(LogLevel level, const char* filename, int line,
                    const string& message) {
  // Nothing.
}

static LogHandler* log_handler_ = &DefaultLogHandler;
static int log_silencer_count_ = 0;

static Mutex* log_silencer_count_mutex_ = nullptr;

Mutex *InitLogSilencerMutex() {
  static Mutex *m = new Mutex();
  return m;
}

void InitLogSilencerCount() {
  log_silencer_count_mutex_ = InitLogSilencerMutex();
}
void InitLogSilencerCountOnce() {
  log_silencer_count_mutex_ = new Mutex();
}

LogMessage& LogMessage::operator<<(const string& value) {
  message_ += value;
  return *this;
}

LogMessage& LogMessage::operator<<(const char* value) {
  message_ += value;
  return *this;
}

// Since this is just for logging, we don't care if the current locale changes
// the results -- in fact, we probably prefer that.  So we use snprintf()
// instead of Simple*toa().
#undef DECLARE_STREAM_OPERATOR
#define DECLARE_STREAM_OPERATOR(TYPE, FORMAT)                       \
  LogMessage& LogMessage::operator<<(TYPE value) {                  \
    /* 128 bytes should be big enough for any of the primitive */   \
    /* values which we print with this, but well use snprintf() */  \
    /* anyway to be extra safe. */                                  \
    char buffer[128];                                               \
    snprintf(buffer, sizeof(buffer), FORMAT, value);                \
    /* Guard against broken MSVC snprintf(). */                     \
    buffer[sizeof(buffer)-1] = '\0';                                \
    message_ += buffer;                                             \
    return *this;                                                   \
  }

DECLARE_STREAM_OPERATOR(char              , "%c"  )
DECLARE_STREAM_OPERATOR(int               , "%d"  )
DECLARE_STREAM_OPERATOR(uint              , "%u"  )
DECLARE_STREAM_OPERATOR(long              , "%ld" )
DECLARE_STREAM_OPERATOR(long long         , "%lld")
DECLARE_STREAM_OPERATOR(unsigned long     , "%lu" )
DECLARE_STREAM_OPERATOR(unsigned long long, "%llu")
DECLARE_STREAM_OPERATOR(double            , "%g"  )
#undef DECLARE_STREAM_OPERATOR

LogMessage::LogMessage(LogLevel level, const char* filename, int line)
  : level_(level), filename_(filename), line_(line) {}
LogMessage::~LogMessage() {}

void LogMessage::Finish() {
  bool suppress = false;

  if (level_ != LOGLEVEL_FATAL) {
    InitLogSilencerCountOnce();
    MutexLock lock(log_silencer_count_mutex_);
    suppress = log_silencer_count_ > 0;
  }

  if (!suppress) {
    log_handler_(level_, filename_, line_, message_);
  }

  if (level_ == LOGLEVEL_FATAL) {
#if PROTOBUF_USE_EXCEPTIONS
    throw FatalException(filename_, line_, message_);
#else
    abort();
#endif
  }
}

void LogFinisher::operator=(LogMessage& other) {
  other.Finish();
}

}  // namespace internal

LogHandler* SetLogHandler(LogHandler* new_func) {
  LogHandler* old = internal::log_handler_;
  if (old == &internal::NullLogHandler) {
    old = NULL;
  }
  if (new_func == NULL) {
    internal::log_handler_ = &internal::NullLogHandler;
  } else {
    internal::log_handler_ = new_func;
  }
  return old;
}

LogSilencer::LogSilencer() {
  internal::InitLogSilencerCountOnce();
  MutexLock lock(internal::log_silencer_count_mutex_);
  ++internal::log_silencer_count_;
};

LogSilencer::~LogSilencer() {
  internal::InitLogSilencerCountOnce();
  MutexLock lock(internal::log_silencer_count_mutex_);
  --internal::log_silencer_count_;
};

static int posix_strerror_r(int err, char *buf, size_t len) {
  // Sanity check input parameters
  if (buf == NULL || len <= 0) {
    errno = EINVAL;
    return -1;
  }

  // Reset buf and errno, and try calling whatever version of strerror_r()
  // is implemented by glibc
  buf[0] = '\000';
  int old_errno = errno;
  errno = 0;
  char* rc = reinterpret_cast<char*>(strerror_r(err, buf, len));

  // Both versions set errno on failure
  if (errno) {
    // Should already be there, but better safe than sorry
    buf[0] = '\000';
    return -1;
  }
  errno = old_errno;

  // If the function succeeded, we can use its exit code to determine the
  // semantics implemented by glibc
  if (!rc) {
    // POSIX is vague about whether the string will be terminated, although
    // is indirectly implies that typically ERANGE will be returned, instead
    // of truncating the string. This is different from the GNU implementation.
    // We play it safe by always terminating the string explicitly.
    buf[len - 1] = '\000';
    return 0;
  } else {
    // GNU semantics detected
    if (rc == buf) {
      return 0;
    } else {
      buf[0] = '\000';
      strncat(buf, rc, len - 1);
      return 0;
    }
  }
}

string StrError(int err) {
  char buf[100];
  int rc = posix_strerror_r(err, buf, sizeof(buf));
  if ((rc < 0) || (buf[0] == '\000')) {
    snprintf(buf, sizeof(buf), "Error number %d", err);
  }
  return buf;
}
