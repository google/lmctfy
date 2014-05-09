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

// Define the "real" CreateInstance() hooks for use in final binaries.
// This is done in a separate .cc file so that it can be linked into any final
// binary that needs these definitions with minimal effort.

#include "system_api/libc_fs_api.h"
#include "system_api/libc_fs_api_impl.h"

using ::system_api::LibcFsApi;
using ::system_api::LibcFsApiImpl;

namespace system_api {

const LibcFsApi *GlobalLibcFsApi() {
  static LibcFsApi *api = new LibcFsApiImpl();
  return api;
}

}  // namespace system_api
