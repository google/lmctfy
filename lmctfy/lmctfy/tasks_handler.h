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

#ifndef SRC_TASKS_HANDLER_H_
#define SRC_TASKS_HANDLER_H_

#include <string>
using ::std::string;
#include <vector>

#include "base/macros.h"
#include "util/safe_types/unix_gid.h"
#include "util/safe_types/unix_uid.h"
#include "util/task/statusor.h"
#include "util/task/status.h"

namespace containers {
namespace lmctfy {

class TasksHandler;

// Interface for factories of TasksHandlers.
//
// Creates new TasksHandlers and Gets existing ones. Is also able to determine
// whether a container exists or what container a TID is running in.
class TasksHandlerFactory {
 public:
  virtual ~TasksHandlerFactory() {}

  // Creates a TasksHandler for a new container. Fails if the container already
  // exists.
  virtual ::util::StatusOr<TasksHandler *> Create(
      const string &container_name, const ContainerSpec &spec) const = 0;

  // Gets (or attaches) a TasksHandler to an existing container. Fails if the
  // container does not exist.
  virtual ::util::StatusOr<TasksHandler *> Get(
      const string &container_name) const = 0;

  // Determines whether the specified container exists. Container names are
  // absolute and resolved.
  virtual bool Exists(const string &container_name) const = 0;

  // Detect in which container the specified TID is running in.
  virtual ::util::StatusOr<string> Detect(pid_t tid) const = 0;

 protected:
  TasksHandlerFactory() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(TasksHandlerFactory);
};

// Handler for tasks in a specific container. This object will own the tasks
// handling for that container. A container should only have one TasksHandler.
class TasksHandler {
 public:
  explicit TasksHandler(const string &container_name)
      : container_name_(container_name) {}
  virtual ~TasksHandler() {}

  // Destroys the underlying TasksHandler. On success, also deletes the object.
  virtual ::util::Status Destroy() = 0;

  // Starts tracking the specified TIDs as part of this handler's container. TID
  // 0 is a shorthand for the current TID. Note that a partially applied
  // TrackTasks() where only some of the TIDs were successfully tracked leaves
  // a container in an undefined state.
  virtual ::util::Status TrackTasks(const ::std::vector<pid_t> &tids) = 0;

  // Delegates ownership of this handler to the specified UNIX user and group.
  // After this operation, the user/group can now TrackTasks and create children
  // TasksHandlers.
  virtual ::util::Status Delegate(::util::UnixUid uid,
                                  ::util::UnixGid gid) = 0;

  // TasksHandlers can populate the machine spec with relavent information.
  virtual ::util::Status PopulateMachineSpec(MachineSpec *spec) const = 0;

  // Whether to list only the current handler, or recursively for all child
  // handlers.
  enum class ListType {
    SELF,
    RECURSIVE,
  };

  // Lists the children containers present in this resource.  Subcontainer names
  // are in their absolute form.
  virtual ::util::StatusOr<::std::vector<string>> ListSubcontainers(
      ListType type) const = 0;

  // Lists the processes running inside this handler.
  virtual ::util::StatusOr<::std::vector<pid_t>> ListProcesses(
      ListType type) const = 0;

  // Lists the threads running inside this handler.
  virtual ::util::StatusOr<::std::vector<pid_t>> ListThreads(
      ListType type) const = 0;

  // Returns the absolute name of the container this TasksHandler manages.
  const string &container_name() const { return container_name_; }

 protected:
  TasksHandler() {}

  const string container_name_;

 private:
  DISALLOW_COPY_AND_ASSIGN(TasksHandler);
};

}  // namespace lmctfy
}  // namespace containers

#endif  // SRC_TASKS_HANDLER_H_
