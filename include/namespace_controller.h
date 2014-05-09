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

#ifndef INCLUDE_NAMESPACE_CONTROLLER_H__
#define INCLUDE_NAMESPACE_CONTROLLER_H__

#include <string>
using ::std::string;
#include <vector>

#include "include/namespaces.pb.h"
#include "util/task/statusor.h"
#include "util/task/status.h"

namespace containers {
namespace nscon {

class NamespaceController;

// NamespaceController API
//
// This file provides access to NamespaceController API that can be used to
// create and manage namespaces. The NamespaceController class can be used to
// create and operate on namespace jail identified by the NamespaceController
// object.

// NamespaceControllerFactory facilitates creation of NamespaceController
// objects.
//
// Class is thread-safe.
class NamespaceControllerFactory {
 public:
  // Returns a new instance of NamespaceControllerFactory iff the status is OK.
  // The returned instance is thread-safe.
  static ::util::StatusOr<NamespaceControllerFactory *> New();

  virtual ~NamespaceControllerFactory() {}

  // Returns a new instance of NamespaceController for the existing namespace
  // jail as identified by given PID. PID is typically that of the INIT process
  // associated with the namespace jail, but any PID in the namespace jail can
  // be used.
  // The returned pointer is now owned by the caller and the caller is
  // responsible for freeing it.
  //
  // Arguments:
  //   pid: PID of INIT or any other process belonging to an existing namespace
  //       jail.
  // Return:
  //   StatusOr: OK iff the operation was successful. On success, the
  //       NamespaceController for the requested namespace jail is returned.
  virtual ::util::StatusOr<NamespaceController *> Get(pid_t pid) const = 0;

  // As above, except the namespace jail is identified using its string handle
  // (as returned by NamespaceController::ToString() function).
  //
  // Arguments:
  //   handlestr: Handle string identifying the existing namespace jail.
  // Return:
  //   StatusOr: OK iff the operation was successful. On success, the
  //       NamespaceController for the requested namespace jail is returned.
  virtual ::util::StatusOr<NamespaceController *> Get(
      const string &handlestr) const = 0;

  // Creates namespaces as per namespace spec and returns an instance of
  // NamespaceController object that identifies the created namespace jail.
  //
  // Arguments:
  //   spec: Specification describing the namespace configuration.
  //   init_argv: The command run as init. Uses nsinit for empty vector.
  // Return:
  //   StatusOr: OK iff the operation was successful. On success the
  //       NamespaceController object is returned for future interactions.
  virtual ::util::StatusOr<NamespaceController *> Create(
      const NamespaceSpec &spec,
      const ::std::vector<string> &init_argv) const = 0;

  // Gets the namespace ID of the specified PID.
  //
  // Arguments:
  //   pid: The PID for which to get the namespace ID.
  // Return:
  //   StatusOr: OK iff the operation was successful. On success the namespace
  //       ID string is populated.
  virtual ::util::StatusOr<string> GetNamespaceId(pid_t pid) const = 0;

 protected:
  NamespaceControllerFactory() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(NamespaceControllerFactory);
};


// NamespaceController: A class that identifies a namespace jail and supports
// various interactions with it.
//
// Class is thread-safe.
class NamespaceController {
 public:
  // Destructor. This doesn't destroy the namespaces themselves.
  virtual ~NamespaceController() {}

  // Run the specified command inside this namespace jail. Multiple instances of
  // run can be active simultaneously.
  //
  // Arguments:
  //   command: The program to execute, with one argument per element. The
  //       first argument must be the absolute path of the command to run.
  // Return:
  //   StatusOr: Status of the operation. OK iff successful. On success, the
  //       PID of the command process is returned.
  virtual ::util::StatusOr<pid_t> Run(
      const ::std::vector<string> &command,
      const RunSpec &run_spec) const = 0;

  // Execute the specified command inside the namespaces.  This replaces the
  // current process image with the specified command. The existing environment
  // is passed to the new process unchanged.
  //
  // Arguments:
  //   command: The program to execute, with one argument per element. The
  //       first argument must be the absolute path of the the command to run.
  // Return:
  //   Status: Status of the operation, iff failure. If this call succeeds,
  //       it never returns.
  virtual ::util::Status Exec(const ::std::vector<string> &command) const = 0;

  // Updates this namespace jail by doing the modifications in the given
  // NamespaceSpec. This operation may not be idempotent. Some fields in the
  // namespace spec may not even be updatable (like the namespaces that were
  // enabled at the time of Create()).
  //
  // Arguments:
  //   spec: The specification containing only the fields that needs to be
  //       updated.
  // Return:
  //   Status: Status of the operation. OK iff successful.
  virtual ::util::Status Update(const NamespaceSpec &spec) = 0;

  // Kills all the processes (alongwith INIT) in the namespace and destroys all
  // namespaces. After success of this call, all other operations (except
  // IsValid()) on this namespace jail may fail.
  //
  // Return:
  //   Status: OK iff the INIT was killed and all the namespaces were destroyed.
  //       On failure, state of the namespace jail is unknown.
  virtual ::util::Status Destroy() = 0;

  // Checks if the namespace jail identified by this controller is still alive.
  //
  // Return:
  //   True iff the given handle is valid, otherwise false.
  virtual bool IsValid() const = 0;

  // Returns the handle string that identifies this namespace jail.
  //
  // Return:
  //   string: handle string that identifies this namespace jail
  virtual const string GetHandleString() const = 0;

  // Returns the PID that can be used to identify this namespace jail.
  //
  // Return:
  //   pid_t: PID that can be used to identify this namespace jail.
  virtual pid_t GetPid() const = 0;

 protected:
  NamespaceController() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(NamespaceController);
};

}  // namespace nscon
}  // namespace containers

#endif  // INCLUDE_NAMESPACE_CONTROLLER_H__
