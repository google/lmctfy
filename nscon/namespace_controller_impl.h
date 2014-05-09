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

#ifndef PRODUCTION_CONTAINERS_NSCON_NAMESPACE_CONTROLLER_IMPL_H__
#define PRODUCTION_CONTAINERS_NSCON_NAMESPACE_CONTROLLER_IMPL_H__

#include "include/namespace_controller.h"

#include <memory>

#include "base/callback.h"
#include "nscon/ns_handle.h"
#include "nscon/ns_util.h"
#include "util/errors.h"
#include "util/process/subprocess.h"

namespace containers {
namespace nscon {

class NamespaceSpec;
class RunSpec;

typedef ResultCallback<SubProcess *> SubProcessFactory;

class NamespaceControllerFactoryImpl : public NamespaceControllerFactory {
 public:
  // Takes ownership of |nshandle_factory| and |subprocess_factory|.
  explicit NamespaceControllerFactoryImpl(NsHandleFactory *nshandle_factory,
                                          SubProcessFactory *subprocess_factory,
                                          const NsUtil *ns_util)
      : nshandle_factory_(nshandle_factory),
        subprocess_factory_(subprocess_factory),
        ns_util_(ns_util) {}
  virtual ~NamespaceControllerFactoryImpl() {}

  // See include/namespace_controller.h for documentation
  // of these methods.
  virtual ::util::StatusOr<NamespaceController *> Get(pid_t pid) const;
  virtual ::util::StatusOr<NamespaceController *> Get(
      const string &handlestr) const;
  virtual ::util::StatusOr<NamespaceController *> Create(
      const NamespaceSpec &spec, const ::std::vector<string> &init_argv) const;
  virtual ::util::StatusOr<string> GetNamespaceId(pid_t pid) const;

 private:
  ::std::unique_ptr<NsHandleFactory> nshandle_factory_;
  // Factory for creating SubProcess instances.
  ::std::unique_ptr<SubProcessFactory> subprocess_factory_;
  ::std::unique_ptr<const NsUtil> ns_util_;

  DISALLOW_COPY_AND_ASSIGN(NamespaceControllerFactoryImpl);
};


class NamespaceControllerImpl : public NamespaceController {
 public:
  virtual ~NamespaceControllerImpl() {}

  // See include/namespace_controller.h for documentation
  // of these methods.
  virtual ::util::StatusOr<pid_t> Run(
      const ::std::vector<string> &command,
      const RunSpec &run_spec) const;
  virtual ::util::Status Exec(const ::std::vector<string> &command) const;
  virtual ::util::Status Update(const NamespaceSpec &spec);
  virtual ::util::Status Destroy();
  virtual bool IsValid() const;
  virtual const string GetHandleString() const;
  virtual pid_t GetPid() const;

 protected:
  // Takes ownership of |nshandle|, but not of |subprocess_factory|.
  explicit NamespaceControllerImpl(const NsHandle *nshandle,
                                   SubProcessFactory *subprocess_factory)
      : nshandle_(nshandle), subprocess_factory_(subprocess_factory) {}

 private:
  ::std::unique_ptr<const NsHandle> nshandle_;
  // Factory for creating SubProcess instances.
  SubProcessFactory *subprocess_factory_;

  friend class NamespaceControllerFactoryImpl;
  friend class NamespaceControllerImplTest;

  DISALLOW_COPY_AND_ASSIGN(NamespaceControllerImpl);
};

}  // namespace nscon
}  // namespace containers

#endif  // PRODUCTION_CONTAINERS_NSCON_NAMESPACE_CONTROLLER_IMPL_H__
