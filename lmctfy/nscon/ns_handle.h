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

#ifndef PRODUCTION_CONTAINERS_NSCON_NS_HANDLE_H__
#define PRODUCTION_CONTAINERS_NSCON_NS_HANDLE_H__

#include <memory>
#include <string>
using ::std::string;

#include "util/task/statusor.h"
#include "util/task/status.h"

namespace containers {
namespace nscon {

class NsHandle;

//
// CookieGenerator
//
// Provides API for generating a cookie for a given process-id. The cookie
// should guarantee protection against PID reuse.
// This is currently used by both NsHandleFactory (for generating cookie) and by
// NsHandle for validation.
//
// Class is thread-safe.
class CookieGenerator {
 public:
  virtual ~CookieGenerator() {}

  // Generates the cookie string from given pid. Note that this function
  // Currently the cookie format is:
  //   * character 'c'
  //   * start time of the <pid> (this is taken from 21st field in
  //                              /proc/<pid>/stat file)
  // Arguments:
  //   pid: PID of the process for which the cookie string needs to be created.
  // Return:
  //   StatusOr: Status of the operation. OK iff successful. On success, the
  //       generated cookie string is returned.
  virtual ::util::StatusOr<string> GenerateCookie(pid_t pid) const;
};

//
// NsHandleFactory
//
// Provides API for getting the NsHandle object.
//
// Class is thread-safe.
class NsHandleFactory {
 public:
  // Static method to get the NsHandleFactory object.
  static ::util::StatusOr<NsHandleFactory *> New();

  virtual ~NsHandleFactory() {}

  // Methods to get the NsHandle instance.
  virtual ::util::StatusOr<const NsHandle *> Get(pid_t pid) const;
  virtual ::util::StatusOr<const NsHandle *> Get(const string &handlestr) const;

 protected:
  // Takes ownership of |cookie_generator|.
  explicit NsHandleFactory(const CookieGenerator *cookie_generator)
      : cookie_generator_(cookie_generator) {}

 private:
  ::std::unique_ptr<const CookieGenerator> cookie_generator_;

  friend class NsHandleFactoryTest;

  DISALLOW_COPY_AND_ASSIGN(NsHandleFactory);
};

//
// NsHandle
//
// NsHandle keeps track of a process. It is valid (IsValid() returns true) as
// long as the process is still alive. It maintains a cookie to protect against
// PID reuse.
//
class NsHandle {
 public:
  // NsHandle does not take ownership of cookie_generator.
  explicit NsHandle(pid_t pid, const string &cookie,
                    const CookieGenerator *cookie_generator)
      : base_pid_(pid), cookie_(cookie), cookie_generator_(cookie_generator) {}

  virtual ~NsHandle() {}

  // Checks if the given nshandle is still valid.
  // Usually the handle becomes invalid when the pid associated with it dies.
  //
  // Return:
  //   True iff the given nshandle is valid.
  virtual bool IsValid() const;

  // String representation of this NsHandle object.
  //
  // Return:
  //   string: String representation of this NsHandle object.
  virtual const string ToString() const;

  // PID associated with this NsHandle object.
  //
  // Return:
  //   pid_t: PID associated with this NsHandle object.
  virtual pid_t ToPid() const;

 private:
  const pid_t base_pid_;
  const string cookie_;
  const CookieGenerator *cookie_generator_;

  friend class NsHandleTest;

  DISALLOW_COPY_AND_ASSIGN(NsHandle);
};

}  // namespace nscon
}  // namespace containers

#endif  // PRODUCTION_CONTAINERS_NSCON_NS_HANDLE_H__
