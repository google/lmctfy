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

#ifndef PRODUCTION_CONTAINERS_NSCON_CONFIGURATOR_NS_CONFIGURATOR_MOCK_H_
#define PRODUCTION_CONTAINERS_NSCON_CONFIGURATOR_NS_CONFIGURATOR_MOCK_H_

#include "nscon/configurator/ns_configurator.h"

#include "base/macros.h"
#include "gmock/gmock.h"

namespace containers {
namespace nscon {

class MockNsConfigurator : public NsConfigurator {
 public:
  MockNsConfigurator() : NsConfigurator(0 /* ns */, nullptr /* ns_util */) {}
  // Helper constructor to pass a valid 'ns'.
  explicit MockNsConfigurator(int ns) : NsConfigurator(ns, nullptr) {}

  ~MockNsConfigurator() override {}

  MOCK_CONST_METHOD2(SetupOutsideNamespace,
                     ::util::Status(const NamespaceSpec &spec, pid_t pid));

  MOCK_CONST_METHOD1(SetupInsideNamespace,
                     ::util::Status(const NamespaceSpec &spec));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockNsConfigurator);
};

}  // namespace nscon
}  // namespace containers

#endif  // PRODUCTION_CONTAINERS_NSCON_CONFIGURATOR_NS_CONFIGURATOR_MOCK_H_
