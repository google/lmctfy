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

#ifndef SRC_CONTROLLERS_CGROUP_CONTROLLER_H_
#define SRC_CONTROLLERS_CGROUP_CONTROLLER_H_

#include <sys/types.h>
#include <string>
using ::std::string;
#include <vector>

#include "base/integral_types.h"
#include "base/logging.h"
#include "base/macros.h"
#include "system_api/kernel_api.h"
#include "lmctfy/controllers/cgroup_factory.h"
#include "lmctfy/controllers/eventfd_notifications.h"
#include "include/lmctfy.pb.h"
#include "util/safe_types/unix_gid.h"
#include "util/safe_types/unix_uid.h"
#include "util/errors.h"
#include "util/file_lines.h"
#include "strings/substitute.h"
#include "util/task/statusor.h"

namespace containers {
namespace lmctfy {

typedef ::system_api::KernelAPI KernelApi;

// Interface for all factories of cgroup-based Controllers.
template <typename ControllerType>
class CgroupControllerFactoryInterface {
 public:
  virtual ~CgroupControllerFactoryInterface() {}

  // Gets a CgroupController for a specific hierarchy_path. The cgroup path for
  // the controller must already exist.
  //
  // Arguments:
  //   hierarchy_path: The path in the cgroup hierarchy that this controller
  //       will manage. i.e.: /test is the hierarchy path for a CPU controller
  //       that manages /dev/cgroup/cpu/test
  // Return:
  //   StatusOr<ControllerType *>: Status of the operation. Iff OK, returns a
  //       new CgroupController. Caller owns the pointer.
  virtual ::util::StatusOr<ControllerType *> Get(
      const string &hierarchy_path) const = 0;

  // Creates a CgroupController for a specific hierarchy_path. If the controller
  // owns the cgroup it will be created and must not already exist. If the
  // controller does not own the cgroup, it is equivalent to a Get() and the
  // cgroup path the controller must already exist
  //
  // Arguments:
  //   hierarchy_path: The path in the cgroup hierarchy that this controller
  //       will manage. i.e.: /test is the hierarchy path for a CPU controller
  //       that manages /dev/cgroup/cpu/test
  // Return:
  //   StatusOr<ControllerType *>: Status of the operation. Iff OK, returns a
  //       new CgroupController. Caller owns the pointer.
  virtual ::util::StatusOr<ControllerType *> Create(
      const string &hierarchy_path) const = 0;

  // Determines whether the specified hierarchy path exists in this cgroup
  // hierarchy.
  //
  // Arguments:
  //   hierarchy_path: The path in the cgroup hierarchy to check for existence.
  //       i.e.: /test is the hierarchy path for a CPU controller that manages
  //       /dev/cgroup/cpu/test
  // Return:
  //   bool: True iff, the specified path exists.
  virtual bool Exists(const string &hierarchy_path) const = 0;

  // Detect the cgroup path of the specified TID.
  //
  // Arguments:
  //   tid: The TID for which to get the cgroup path.
  // Return:
  //   StatusOr<string>: Status or the operation. Iff OK, the cgroup path is
  //       populated.
  virtual ::util::StatusOr<string> DetectCgroupPath(pid_t tid) const = 0;

  // Return the name of the cgroup hierarchy this factory creates controllers
  // for.
  //
  // Return:
  //   string: The name of the hierarchy. i.e.: cpuacct.
  virtual string HierarchyName() const = 0;
};

// A base factory for CgroupControllers.
//
// A minimal extension of this factory defines a constructor and destructor and
// uses the generated Get()/Create(). See this class' test for an example of one
// such extension. Get()//Create() can be overwritten if a more customized
// creation of CgroupControllers is needed. It is highly encouraged to use the
// CgroupFactory as it handles cgroup discovery, generation, checking, and
// creation.
//
// Class is thread-safe.
template <typename ControllerType, CgroupHierarchy hierarchy_type>
class CgroupControllerFactory
    : public CgroupControllerFactoryInterface<ControllerType> {
 public:
  static CgroupHierarchy HierarchyType() { return hierarchy_type; }

  // Does not take ownership of cgroup_factory, kernel, or
  // eventfd_notifications.
  CgroupControllerFactory(const CgroupFactory *cgroup_factory,
                          const KernelApi *kernel,
                          EventFdNotifications *eventfd_notifications,
                          bool owns_cgroup)
      : cgroup_factory_(CHECK_NOTNULL(cgroup_factory)),
        kernel_(CHECK_NOTNULL(kernel)),
        owns_cgroup_(owns_cgroup),
        eventfd_notifications_(CHECK_NOTNULL(eventfd_notifications)) {}

  // Does not take ownership of cgroup_factory, kernel, or
  // eventfd_notifications.
  CgroupControllerFactory(const CgroupFactory *cgroup_factory,
                          const KernelApi *kernel,
                          EventFdNotifications *eventfd_notifications)
      : CgroupControllerFactory(CHECK_NOTNULL(cgroup_factory), kernel,
                                eventfd_notifications,
                                cgroup_factory->OwnsCgroup(hierarchy_type)) {}

  ~CgroupControllerFactory() override {}

  ::util::StatusOr<ControllerType *> Get(const string &hierarchy_path)
      const override {
    // Get the cgroup.
    ::util::StatusOr<string> statusor =
        cgroup_factory_->Get(hierarchy_type, hierarchy_path);
    if (!statusor.ok()) {
      return statusor.status();
    }

    return new ControllerType(hierarchy_path, statusor.ValueOrDie(),
                              owns_cgroup_, kernel_, eventfd_notifications_);
  }

  ::util::StatusOr<ControllerType *> Create(const string &hierarchy_path)
      const override {
    // Create the cgroup.
    string cgroup_path;
    if (owns_cgroup_) {
      cgroup_path = RETURN_IF_ERROR(
          cgroup_factory_->Create(hierarchy_type, hierarchy_path));
    } else {
      cgroup_path = RETURN_IF_ERROR(
          cgroup_factory_->Get(hierarchy_type, hierarchy_path));
    }

    return new ControllerType(hierarchy_path, cgroup_path, owns_cgroup_,
                              kernel_, eventfd_notifications_);
  }

  bool Exists(const string &hierarchy_path) const override {
    // If a Get() on the hierarchy succeeds, the hierarchy is ready and thus
    // exists.
    return cgroup_factory_->Get(hierarchy_type, hierarchy_path).ok();
  }

  ::util::StatusOr<string> DetectCgroupPath(pid_t tid) const override {
    return cgroup_factory_->DetectCgroupPath(tid, hierarchy_type);
  }

  string HierarchyName() const override {
    return cgroup_factory_->GetHierarchyName(hierarchy_type);
  }

 private:
  // Factory for cgroup paths used to create controllers.
  const CgroupFactory *cgroup_factory_;

  // Wrapper for all calls to the kernel.
  const KernelApi *kernel_;

  // Whether this controller owns the underlying cgroup mount.
  const bool owns_cgroup_;

  // EventFd-based notifications.
  EventFdNotifications *eventfd_notifications_;

  DISALLOW_COPY_AND_ASSIGN(CgroupControllerFactory);
};

// A CgroupController is a wrapper around all operations around a specific path
// in a cgroup hierarchy. It is of a particular hierarchy type (i.e.: cpu) and
// controls a specific path inside that hierarchy.
//
// e.g.:
// Lets assume that all cgroups are mounted in:
//   /dev/cgroup
// So that CPU is mounted in:
//   /dev/cgroup/cpu
// And a CPU CgroupController that controls the hierarchy path /alloc1 controls
// the absolute path:
//   /dev/cgroup/cpu/alloc1
//
// Since multiple cgroup hierarchies can be co-mounted, CgroupControllers also
// have the concept of "cgroup ownership." If the controller owns the underlying
// cgroup it can create/destroy it. Else, it cannot.
//
// Class is thread-safe.
class CgroupController {
 public:
  // Callback used to notify of the occurence of an event. The status specified
  // the status of the event, which is OK on delivery of an event and an error
  // value if an error occured.
  typedef Callback1< ::util::Status> EventCallback;

  virtual ~CgroupController();

  // Destroys the underlying cgroup_path (if this controller owns it) and
  // deletes this object.
  //
  // Return:
  //   Status: Status of the operation. Iff OK, the operation was successful.
  virtual ::util::Status Destroy();

  // Enters the specified TID into this controller (if this controller owns it).
  //
  // Arguments:
  //   tid: The TID to enter into this controller.
  // Return:
  //   Status: Status of the operation. Iff OK, the operation was successful.
  virtual ::util::Status Enter(pid_t tid);

  // Delegates the controller to the specified user and group. This user/group
  // can now enter into this cgroup and create children cgroups.
  //
  // Arguments:
  //   uid: UNIX user ID of the user to delegate to.
  //   uid: UNIX group ID of the group to delegate to.
  // Return:
  //   Status: Status of the operation. Iff OK, the operation was successful.
  virtual ::util::Status Delegate(::util::UnixUid uid,
                                  ::util::UnixGid gid);

  // Sets the limit in the number of children for this cgroup
  //
  // Arguments:
  //   limit: The max number of children allowed
  // Return:
  //   Status: Status of the operation. Iff OK, the operation was successful.
  virtual ::util::Status SetChildrenLimit(int64 limit);

  // Gets the threads in this cgroup.
  //
  // Return:
  //   StatusOr<vector<pid_t>>: Status of the operation. Iff OK, the list of
  //       thread PIDs is provided.
  virtual ::util::StatusOr< ::std::vector<pid_t>> GetThreads() const;

  // Gets the processes in this cgroup.
  //
  // Return:
  //   StatusOr<vector<pid_t>>: Status of the operation. Iff OK, the list of
  //       process PIDs is provided.
  virtual ::util::StatusOr< ::std::vector<pid_t>> GetProcesses() const;

  // Gets the subcontainers of this cgroup. By default this is considered the
  // subdirectories of this cgroup.
  //
  // Return:
  //   StatusOr<vector<string>>: Status of the operation. Iff OK, the list of
  //       subcontainers is populated. These names are relative to the current
  //       container.
  virtual ::util::StatusOr< ::std::vector<string>> GetSubcontainers() const;

  // Gets the number of children allowed for this cgroup
  //
  // Return:
  //   StatusOr<vector<string>>: Status of the operation. Iff OK, the number of
  //       allowable is populated.
  virtual ::util::StatusOr<int64> GetChildrenLimit() const;

  // Whether to enable or disable cloning the parent's configuration into the
  // children's cgroups.
  //
  // Arguments:
  //   value: Whether to enable/disable inheriting the parent's configuration.
  // Return:
  //   Status: Status of the operation. OK on success.
  virtual ::util::Status EnableCloneChildren();
  virtual ::util::Status DisableCloneChildren();

  virtual ::util::Status PopulateMachineSpec(MachineSpec *spec) const;

  bool owns_cgroup() const { return owns_cgroup_; }

  CgroupHierarchy type() const { return type_; }

  // Relative path to the container in this cgroup hierarchy.
  const string &hierarchy_path() const { return hierarchy_path_; }

 protected:
  // Arguments:
  //   type: The type of hierarchy this controller affects.
  //   hierarchy_path: The relative path to the cgroup.
  //   cgroup_path: See the documentation for cgroup_path_ below.
  //   owns_cgroup: Whether this controller owns the underlying cgroup_path and
  //       it can perform creation/destruction on it. If N cgroup hierarchies
  //       are co-mounted, only one of them can own the cgroup.
  //   kernel: Wrapper for all kernel calls. Does not take ownership.
  //   eventfd_notifications: Set of eventfd-based notifications. Does not take
  //       ownership.
  CgroupController(CgroupHierarchy type, const string &hierarchy_path,
                   const string &cgroup_path,
                   bool owns_cgroup, const KernelApi *kernel,
                   EventFdNotifications *eventfd_notifications);

  // Writes the specified value in the cgroup_file of this controller.
  //
  // e.g.:
  // SetParamInt("tasks", 42) will write 42 to: cgroup_path + "/tasks"
  //
  // Arguments:
  //   cgroup_file: The cgroup file to write to, e.g. "memory.limit_in_bytes"
  //   value: The value to write to the file.
  // Return:
  //   Status: Status of the operation. Iff OK, the write was successful. If the
  //       file could not be found or accessed, NOT_FOUND is returned.
  virtual ::util::Status SetParamBool(const string &cgroup_file, bool value);
  virtual ::util::Status SetParamInt(const string &cgroup_file, int64 value);
  virtual ::util::Status SetParamString(const string &cgroup_file,
                                        const string &value);

  // Reads the a value of a certain type from the specified cgroup_file of this
  // controller.
  //
  // GetParamLines() gets an iterator to the lines of a file.
  //
  // e.g.:
  // GetParamString("tasks") will read "42\n43\n" from: cgroup_path + "/tasks"
  //
  // Arguments:
  //   cgroup_file: The cgroup file to read from, e.g. "memory.limit_in_bytes"
  // Return:
  //   StatusOr: Status of the operation. Iff OK, the value read from the file
  //       is provided. If the file is not available on the machine, NOT_FOUND
  //       is returned.
  virtual ::util::StatusOr<bool> GetParamBool(const string &cgroup_file) const;
  virtual ::util::StatusOr<int64> GetParamInt(const string &cgroup_file) const;
  virtual ::util::StatusOr<string> GetParamString(
      const string &cgroup_file) const;
  virtual ::util::StatusOr< ::util::FileLines> GetParamLines(
      const string &cgroup_file) const;

  // Gets the subdirectories of this controller. This function adds the
  // directories to the end of the provided vector.
  //
  // Arguments:
  //   path: The path at which to get the subdirectories.
  //   entries: The vector to add the entries to.
  // Return:
  //   Status: Status of the operation.
  virtual ::util::Status GetSubdirectories(
      const string &path, ::std::vector<string> *entries) const;

  // Recursively deletes all directories at the given path.
  //
  // Arguments:
  //   path: The path under which to delete all subdirectories.
  // Return:
  //   StatusOr: Status of the operation.
  virtual ::util::Status DeleteCgroupHierarchy(const string &path) const;

  // Registers the specified notification for the cgroup_file event given the
  // specified arguments.
  //
  // Arguments:
  //   cgroup_file: The cgroup file to register an event for, e.g.
  //       "memory.oom_control"
  //   arguments: The arguments for the event.
  //   callback: The permanent callback to use when the event is triggered. Must
  //       not be a nullptr. Takes ownership.
  // Return:
  //   Status: Status of the operation. Iff OK, the registration was successful.
  virtual ::util::StatusOr<ActiveNotifications::Handle> RegisterNotification(
      const string &cgroup_file, const string &arguments,
      EventCallback *callback);

  // Helper function to write a string to a file.
  //
  // Arguments:
  //   file_path: Full path to the file to write to, e.g. /proc/sys/file
  //   value: The string to write to the file.
  // Return:
  //   Status: Status of the operation. Iff OK, the write was successful.
  virtual ::util::Status WriteStringToFile(const string &file_path,
                                           const string &value) const;

  // TODO(jonathanw): Fix this to actually return the cgroup name.
  const string &cgroup_name() const { return cgroup_path_; }

 private:
  // Helper function to read a file's contents to a string.
  //
  // Arguments:
  //   file_path: Full path to the file to read from, e.g. /proc/sys/file
  // Return:
  //   Status: Status of the operation. Iff OK, the read was successful.
  //       NOT_FOUND is returned if the file was not found.
  virtual ::util::StatusOr<string> ReadStringFromFile(
      const string &file_path) const;

  // Returns the absolute path to the specified cgroup_file.
  // ParentCgroupFilePath returns the same path but on the parent. The result of
  // ParentCgroupFilaPath on / or non-hierarchical resource hierarchies is
  // undefined.
  //
  // e.g.:
  // CgroupFilePath("tasks") -> cgroup_path_ + "tasks"
  //
  // Arguments:
  //   cgroup_file: The cgroup file to translate. e.g. "memory.limit_in_bytes"
  // Return:
  //   string: The cgroup path of the specified file. e.g.:
  //       "/dev/cgroup/memory/test/memory.limit_in_bytes"
  string CgroupFilePath(const string &cgroup_file) const;

  // Parses a PID on each line of the specified file and returns them.
  //
  // Arguments:
  //   cgroup_file: The cgroup file to read PIDs from,
  //       e.g. "tasks"
  // Return:
  //   StatusOr: Status of the operation. Iff OK, the value read from the file
  //       is provided.
  ::util::StatusOr< ::std::vector<pid_t>> GetPids(
      const string &cgroup_file) const;

  // The cgroup hierarchy type controlled by this controller.
  const CgroupHierarchy type_;

  // Relative path to the container in this cgroup hierarchy.
  const string hierarchy_path_;

  // Absolute path to the cgroup directory of this cgroup controller. Note that
  // this may not always be a concat of the cgroup mount point and the
  // container name since a resource handler may have a different mapping.
  //
  // e.g.:
  // CPU maps all batch tasks to /batch. So container of batch task /task1 is
  // mapped to /dev/cgroup/cpu/batch/task1
  const string cgroup_path_;

  // Whether this controller owns the underlying cgroup_path and it can perform
  // creation/destruction on it.
  const bool owns_cgroup_;

  // Wrapper for all calls to the kernel.
  const KernelApi *kernel_;

  // EventFd-based notifications.
  EventFdNotifications *eventfd_notifications_;

  friend class CgroupControllerTest;
  friend class GetParamLinesTest;
  friend class CgroupControllerRealTest;

  DISALLOW_COPY_AND_ASSIGN(CgroupController);
};

}  // namespace lmctfy
}  // namespace containers

#endif  // SRC_CONTROLLERS_CGROUP_CONTROLLER_H_
