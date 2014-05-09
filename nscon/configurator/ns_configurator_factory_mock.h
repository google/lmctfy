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

#ifndef PRODUCTION_CONTAINERS_NSCON_CONFIGURATOR_NS_CONFIGURATOR_FACTORY_MOCK_H_
#define PRODUCTION_CONTAINERS_NSCON_CONFIGURATOR_NS_CONFIGURATOR_FACTORY_MOCK_H_

#include "nscon/configurator/ns_configurator_factory.h"

#include "base/macros.h"
#include "gmock/gmock.h"

namespace containers {
namespace nscon {

class MockNsConfiguratorFactory : public NsConfiguratorFactory {
 public:
  ~MockNsConfiguratorFactory() override {}

  MockNsConfiguratorFactory() : NsConfiguratorFactory(nullptr /* ns_util */) {}

  MOCK_CONST_METHOD1(Get, ::util::StatusOr<NsConfigurator *>(int ns));
  MOCK_CONST_METHOD0(GetFilesystemConfigurator,
                     ::util::StatusOr<NsConfigurator *>());
  MOCK_CONST_METHOD0(GetMachineConfigurator,
                     ::util::StatusOr<NsConfigurator *>());

 private:
  DISALLOW_COPY_AND_ASSIGN(MockNsConfiguratorFactory);
};

}  // namespace nscon
}  // namespace containers

#endif  // PRODUCTION_CONTAINERS_NSCON_CONFIGURATOR_NS_CONFIGURATOR_FACTORY_MOCK_H_
