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

#ifndef PRODUCTION_CONTAINERS_NSCON_CONFIGURATOR_NS_CONFIGURATOR_FACTORY_H_
#define PRODUCTION_CONTAINERS_NSCON_CONFIGURATOR_NS_CONFIGURATOR_FACTORY_H_

#include "base/macros.h"
#include "util/task/status.h"
#include "util/task/statusor.h"

namespace containers {
namespace nscon {

class NsUtil;
class NsConfigurator;

// NsConfiguratorFactory
//
// This class provides an interface to obtain an instance of NsConfigurator.
//
// This class is thread-safe, because it does not perform the actual namespace
// configuration.
//
class NsConfiguratorFactory {
 public:
  // Returns a newly constructed instance of NsConfiguratorFactory
  // Caller takes the ownership of the returned value.
  // Does NOT take ownership of |ns_util|.
  static ::util::StatusOr<NsConfiguratorFactory *> New(NsUtil *ns_util);

  virtual ~NsConfiguratorFactory() {}

  // Based on the namespace indicated by |ns|, it instantiates the appropriate
  // implementation of NsConfigurator interface. Caller takes ownership of the
  // returned value.
  virtual ::util::StatusOr<NsConfigurator *> Get(int ns) const;

  virtual ::util::StatusOr<NsConfigurator *> GetFilesystemConfigurator() const;

  virtual ::util::StatusOr<NsConfigurator *> GetMachineConfigurator() const;

 protected:
  // Constructor
  // Does NOT take ownership of |ns_util|
  explicit NsConfiguratorFactory(NsUtil *ns_util) : ns_util_(ns_util) {}

 private:
  NsUtil *ns_util_;

  DISALLOW_COPY_AND_ASSIGN(NsConfiguratorFactory);
};

}  // namespace nscon
}  // namespace containers

#endif  // PRODUCTION_CONTAINERS_NSCON_CONFIGURATOR_NS_CONFIGURATOR_FACTORY_H_
