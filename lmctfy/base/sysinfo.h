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

#ifndef BASE_SYSINFO_H__
#define BASE_SYSINFO_H__

#include <sys/types.h>

// Return the thread id of the current thread, as told by the system.
// No two currently-live threads implemented by the OS shall have the same ID.
// Thread ids of exited threads may be reused.   Multiple user-level threads
// may have the same thread ID if multiplexed on the same OS thread.
//
// On Linux, you may send a signal to the resulting ID with kill().  However,
// it is recommended for portability that you use pthread_kill() instead.
pid_t GetTID();

#endif  // BASE_SYSINFO_H__
