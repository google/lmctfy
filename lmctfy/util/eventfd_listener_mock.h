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

// Mock class for EventfdListener facilitates testing any client code.

#ifndef UTIL_EVENTFD_LISTENER_MOCK_H_
#define UTIL_EVENTFD_LISTENER_MOCK_H_

#include "util/eventfd_listener.h"
#include "gmock/gmock.h"

namespace util {

class MockEventfdListener : public EventfdListener {
 public:
  MockEventfdListener(const ::system_api::KernelAPI& kernel,
                      const string& thread_name,
                      EventReceiverInterface* er,
                      bool joinable,
                      int max_multiplexed_events)
      : EventfdListener(kernel,
                        thread_name,
                        er,
                        joinable,
                        max_multiplexed_events) {}
  MOCK_METHOD5(Add, bool(const string &, const string &, const string &,
                         const string &, EventReceiverInterface *er));
  MOCK_METHOD0(Start, void());
  MOCK_METHOD0(Stop, void());
  MOCK_METHOD0(IsNotRunning, bool());
  MOCK_METHOD0(WaitUntilStopped, void());
};

class MockEventfdListenerFactory : public EventfdListenerFactory {
 public:
  MOCK_METHOD5(NewEventfdListener,
               EventfdListener*(const ::system_api::KernelAPI& kernel,
                                const string& thread_name,
                                EventReceiverInterface* er,
                                bool joinable,
                                int max_multiplexed_events));
};

class MockEventReceiverInterface : public EventReceiverInterface {
 public:
  MOCK_METHOD2(ReportEvent, bool(const string &name, const string &args));
  MOCK_METHOD2(ReportError, void(const string &name, EventfdListener *efdl));
  MOCK_METHOD2(ReportExit, void(const string &name, EventfdListener *efdl));
};

}  // namespace util

#endif  // UTIL_EVENTFD_LISTENER_MOCK_H_
