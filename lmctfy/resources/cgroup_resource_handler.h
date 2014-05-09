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

#ifndef SRC_RESOURCES_CGROUP_RESOURCE_HANDLER_H_
#define SRC_RESOURCES_CGROUP_RESOURCE_HANDLER_H_

#include <sys/types.h>
#include <map>
#include <string>
using ::std::string;
#include <vector>

#include "base/macros.h"
#include "system_api/kernel_api.h"
#include "lmctfy/resource_handler.h"
#include "include/lmctfy.h"
#include "include/lmctfy.pb.h"
#include "util/task/statusor.h"

namespace containers {
namespace lmctfy {

class CgroupController;
class CgroupFactory;

typedef ::system_api::KernelAPI KernelApi;


// Abstract class that provides some useful methods and members for handling
// cgroup-based resource handler factories.
//
// A cgroup-based ResourceHandler factory only needs to implement:
// - GetResourceHandler()
// - CreateResourceHandler()
// - NewResourceHandler()
// Take a look at the documentation of each method below for details and
// examples.
//
// If the default behavior for Get(), Create(), and InitMachine() is not
// satisfactory, those can be overwritten as well.
//
// Class is thread-safe.
class CgroupResourceHandlerFactory : public ResourceHandlerFactory {
 public:
  // Does not own cgroup_factory or kernel. Cgroup_hierarchies are the cgroup
  // hierarchies used by this ResourceHandler.
  CgroupResourceHandlerFactory(
      ResourceType resource_type,
      CgroupFactory *cgroup_factory,
      const KernelApi *kernel);
  ~CgroupResourceHandlerFactory() override {}

  // Default implementation uses GetResourceHandler().
  ::util::StatusOr<ResourceHandler *> Get(const string &container_name)
      override;

  // Default implementation uses CreateResourceHandler() and then sets the
  // container spec through ResourceHandler's Update().
  ::util::StatusOr<ResourceHandler *> Create(
      const string &container_name, const ContainerSpec &spec) override;

  // Default implementation is a no-op.
  ::util::Status InitMachine(const InitSpec &spec) override {
    return ::util::Status::OK;
  }

 protected:
  // Gets or creates a ResourceHandler.
  //
  // GetResourceHandler() is called during Get() when the container is expected
  // to already exist.
  // CreateResourceHandler() is called during Create() when the container is not
  // expected to exist and thus the spec of the container is provided.
  //
  // To get/create a ResourceHandler the user must translate from container name
  // to the hierarchy name and then instantiate the controllers and the
  // ResourceHandler.
  //
  // The name translation aims to capture how this ResourceHandler maps
  // containers to cgroup directories (i.e.: every container has its own cgroup
  // directory or every batch container is in the same cgroup directory).
  //
  // e.g. for translations:
  // 1:1 mapping:
  //   "/test" -> "/test"
  // Batch tasks to batch cgroup:
  //   "/test" (with spec that specified batch) -> "/batch/test"
  //
  // Arguments:
  //   container_name: The name of the container.
  //   spec: The specification used to build the container.
  // Return:
  //   StatusOr<ResourceHandler *>: Status of the operation. Iff OK, a
  //       ResourceHandler instance owned by the called.
  virtual ::util::StatusOr<ResourceHandler *> GetResourceHandler(
      const string &container_name) const = 0;
  virtual ::util::StatusOr<ResourceHandler *> CreateResourceHandler(
      const string &container_name,
      const ContainerSpec &spec) const = 0;

  // Wrapper for all calls to the kernel.
  const KernelApi *kernel_;

 private:
  // Whether the this ResourceHandler is supported in this system.
  bool is_supported_;

  DISALLOW_COPY_AND_ASSIGN(CgroupResourceHandlerFactory);
};

// Convenience macro for use in statistics. Sets the specified statistic if the
// statusor is OK, ignores it if it is NOT_FOUND, and returns the error
// otherwise. Use the _VAL version with Strong types (e.g.: Bytes) as it will
// call .value() on the type before sending it to the set_fn.
//
// Example use:
//
// SET_IF_PRESENT(memory_controller_->GetUsage(), memory_stats->set_usage);
//
// Arguments:
//   statusor: The StatusOr<result> of the operation.
//   set_fn: The function used to set the stat. This should be the full path
//       that must be used to call the function. i.e.: memory->set_usage.
#define SET_IF_PRESENT(statusor, set_fn)                                      \
  do {                                                                        \
    const auto &_statusor = statusor;                                         \
    if (_statusor.ok()) {                                                     \
      set_fn(_statusor.ValueOrDie());                                         \
    } else if (_statusor.status().error_code() != ::util::error::NOT_FOUND) { \
      return _statusor.status();                                              \
    }                                                                         \
  } while (0)
#define SET_IF_PRESENT_SAVE_FAILURE(statusor, set_fn, failure_status)         \
  do {                                                                        \
    const auto &_statusor = statusor;                                         \
    if (_statusor.ok()) {                                                     \
      set_fn(_statusor.ValueOrDie());                                         \
    } else if (_statusor.status().error_code() != ::util::error::NOT_FOUND) { \
      failure_status = _statusor.status();                                    \
    }                                                                         \
  } while (0)

#define SET_IF_PRESENT_VAL(statusor, set_fn)                                  \
  do {                                                                        \
    const auto &_statusor = statusor;                                         \
    if (_statusor.ok()) {                                                     \
      set_fn(_statusor.ValueOrDie().value());                                 \
    } else if (_statusor.status().error_code() != ::util::error::NOT_FOUND) { \
      return _statusor.status();                                              \
    }                                                                         \
  } while (0)
#define SET_IF_PRESENT_VAL_SAVE_FAILURE(statusor, set_fn, failure_status)     \
  do {                                                                        \
    const auto &_statusor = statusor;                                         \
    if (_statusor.ok()) {                                                     \
      set_fn(_statusor.ValueOrDie().value());                                 \
    } else if (_statusor.status().error_code() != ::util::error::NOT_FOUND) { \
      failure_status = _statusor.status();                                    \
    }                                                                         \
  } while (0)

#define SAVE_IF_ERROR(status, failure_status)  \
  do {                                         \
    if (!status.ok()) {                        \
      failure_status = status;                 \
    }                                          \
  } while (0)

// Abstract class that provides some useful methods and members for handling
// cgroup-based resource handlers. Implements a generic Destroy() and Enter(),
// see below for details.
//
// A cgroup-based ResourceHandler has to implement:
// - Stats()
// - Spec()
// - RegisterNotification()
// - DoUpdate()
// - VerifyFullSpec()
// - RecursiveFillDefaults()
// - FillSpec()
// These are the core details of how the resource handler is implemented. Take a
// look at the documentaion in the ResourceHandler class definition for more
// details of expected behavior.
//
// If the default behavior for Destroy() and Enter() is not satisfactory, those
// can be overwritten as well.
//
// NOTE: Some classes override Update() but don't implement methods:
// DoUpdate(), VerifyFullSpec(), RecursiveFillDefaults() and FillSpec().
// This approach is deprecated and will be changed as soon as possible.
//
// Class is thread-safe.
class CgroupResourceHandler : public ResourceHandler {
 public:
  // Does not own kernel. Takes ownership of controllers.
  CgroupResourceHandler(
      const string &container_name,
      ResourceType resource_type,
      const KernelApi *kernel,
      const ::std::vector<CgroupController *> &controllers);
  ~CgroupResourceHandler() override;

  ::util::Status CreateResource(const ContainerSpec &spec) final;

  // TODO(kyurtsever) Make this method final and make DoUpdate,
  // FillWithDefaults and VerifyFullSpec methods pure virtual.
  // See these methods TODO for sufficient condition.
  //
  // If you don't override Update you _have_ to override DoUpdate,
  // FillWithDefaults and VerifyFullSpec, and also your Spec _has_ to conform
  // to semantic described below (writing only to unset fields).
  ::util::Status Update(const ContainerSpec &spec,
                        Container::UpdatePolicy policy) override;
  virtual ::util::Status Stats(Container::StatsType type,
                               ContainerStats *output) const = 0;
  // Fills all unset fields of @spec with values read from the machine.
  // TODO(kyurtsever) Make all implementations conform to this semantic.
  virtual ::util::Status Spec(ContainerSpec *spec) const = 0;
  virtual ::util::StatusOr<Container::NotificationId> RegisterNotification(
      const EventSpec &spec, Callback1< ::util::Status> *callback) = 0;

  // Destroys all controllers and iff OK deletes this object.
  ::util::Status Destroy() override;

  // Enter the specified TIDs into all controllers.
  ::util::Status Enter(const ::std::vector<pid_t> &tids) override;

  ::util::Status Delegate(::util::UnixUid uid,
                          ::util::UnixGid gid) override;

  ::util::Status PopulateMachineSpec(MachineSpec *spec) const override;

 protected:
  // Perform any setup that only occurs at container creation time. This setup
  // will be followed by an Update(). This should be overridden to handle the
  // setup for this specific resource--general setup that happens in all
  // resources will occur in CreateResource
  virtual ::util::Status CreateOnlySetup(const ContainerSpec &spec) {
    return ::util::Status::OK;
  }

  // TODO(kyurtsever) Make methods DoUpdate, RecursiveFillDefaults and
  // VerifyFullSpec pure virtual when all ContainerSpec repeated fields are in
  // submessages (always set semantics for repeated fields).

  // Updates the machine with exactly supplied fields. Doesn't fill defaults.
  // Doesn't verify correctness.
  virtual ::util::Status DoUpdate(const ContainerSpec &spec);

  // Fills all unset fields of @spec with default values.
  // Unset parameters in submessages and repeated submessages are also filled.
  virtual void RecursiveFillDefaults(ContainerSpec *spec) const;

  // Checks if container state described in @spec is correct.
  virtual ::util::Status VerifyFullSpec(const ContainerSpec &spec) const;

  // List of controllers.
  ::std::vector<CgroupController *> controllers_;

  // Wrapper for all calls to the kernel.
  const KernelApi *kernel_;

 private:
  // Adjust @update_spec according to @policy.
  ::util::Status Adjust(Container::UpdatePolicy policy,
                        ContainerSpec *update_spec) const;

  // Check if update described in @update_spec is valid.
  ::util::Status Validate(const ContainerSpec &update_spec) const;

  DISALLOW_COPY_AND_ASSIGN(CgroupResourceHandler);
};

}  // namespace lmctfy
}  // namespace containers

#endif  // SRC_RESOURCES_CGROUP_RESOURCE_HANDLER_H_
