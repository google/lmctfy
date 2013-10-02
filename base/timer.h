// Copyright 2010-2013 Google
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

#ifndef BASE_TIMER_H_
#define BASE_TIMER_H_

#include "base/macros.h"

class WallTimer {
 public:
  WallTimer();

  void Start();
  void Stop();
  bool Reset();
  void Restart();
  bool IsRunning() const;
  int64 GetInMs() const;
  double Get() const;
  static int64 GetTimeInMicroSeconds();

 private:
  int64 start_usec_;  // start time in microseconds.
  int64 sum_usec_;    // sum of time diffs in microseconds.
  bool has_started_;

  DISALLOW_COPY_AND_ASSIGN(WallTimer);
};

// A WallTimer clone meant to support SetClock(), for unit testing. But for now
// we just use WallTimer directly.
typedef WallTimer ClockTimer;

// This class is currently using the same counter as the WallTimer class, but
// is supposed to use the cpu cycle counter.
// TODO(user): Implement it properly.
class CycleTimer {
 public:
  CycleTimer();
  void Reset();
  void Start();
  void Restart();
  void Stop();
  int64 GetCycles() const;
  int64 GetInUsec() const;
  int64 GetInMs() const;
 private:
  enum State {
    INIT,
    STARTED,
    STOPPED
  };
  int64 time_in_us_;
  State state_;
};

// As for the CycleTimer, this is not using the real cycle unit, and just
// assumes one usec per cycle.
class CycleTimerBase {
 public:
  static double CyclesToSeconds(int64 cycles) {
    return static_cast<double>(cycles) / 1000000.0;
  }

  static int64 UsecToCycles(int64 usec) {
    return usec;
  }
};

class ScopedWallTime {
 public:
  // We do not own the pointer. The pointer must be valid for the duration
  // of the existence of the ScopedWallTime instance. Not thread safe for
  // aggregate_time.
  explicit ScopedWallTime(double* aggregate_time);
  ~ScopedWallTime();

 private:
  double* aggregate_time_;

  // When the instance was created.
  WallTimer timer_;

  DISALLOW_COPY_AND_ASSIGN(ScopedWallTime);
};

// ----------------------------------------------------------------------
// ElapsedTimer
//    An elapsed timer is a little different from a normal timer;
//    it doesn't have the standard interface.  It's meant to be
//    enclosed in a block; when the block exits, it logs (at
//    level INFO) how much time the block took.
//       The first argument is a prefix, the second can be false to
//    cause the timer to do nothing (presumably it's a run-time expression),
//    the third is a minimum time (in seconds) below which we don't print.
// ----------------------------------------------------------------------

class ElapsedTimer {
 public:
  inline ElapsedTimer(const char* prefix);  // NOLINT
  inline ElapsedTimer(const char* prefix, bool active, double mintime);
  ~ElapsedTimer();

 private:
  const char *prefix_;
  const double mintime_;
  WallTimer timer_;

  DISALLOW_COPY_AND_ASSIGN(ElapsedTimer);
};

inline ElapsedTimer::ElapsedTimer(const char* prefix) :
  prefix_(prefix), mintime_(0.0), timer_() {
  timer_.Start();
}

inline ElapsedTimer::ElapsedTimer(const char* prefix, bool active,
                                  double mintime) :
  prefix_(prefix), mintime_(mintime), timer_() {
  if (active) timer_.Start();
}

#endif  // BASE_TIMER_H_

