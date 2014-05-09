// Defines a strong-int type for managing time values.

#ifndef UTIL_SAFE_TYPES_TIME_H_
#define UTIL_SAFE_TYPES_TIME_H_

#include "base/integral_types.h"
#include "base/walltime.h"
#include "util/intops/safe_int.h"  // IWYU pragma: export

namespace util {

DEFINE_SAFE_INT_TYPE(Seconds, int64, ::util_intops::LogFatalOnError);
DEFINE_SAFE_INT_TYPE(Milliseconds, int64, ::util_intops::LogFatalOnError);
DEFINE_SAFE_INT_TYPE(Microseconds, int64, ::util_intops::LogFatalOnError);
DEFINE_SAFE_INT_TYPE(Nanoseconds, int64, ::util_intops::LogFatalOnError);

inline Microseconds TimeNowMicroseconds() {
  return Microseconds(WallTime_Now() * 1000000);
}

}  // namespace util

#endif  // UTIL_SAFE_TYPES_TIME_H_
