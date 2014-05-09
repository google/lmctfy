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

#include "strings/charset.h"

#include <string.h>
#include "strings/stringpiece.h"

namespace strings {

CharSet::CharSet() {
  memset(bits_, 0, sizeof(bits_));
}

CharSet::CharSet(const char* characters) {
  memset(bits_, 0, sizeof(bits_));
  for (; *characters != '\0'; ++characters) {
    Add(*characters);
  }
}

CharSet::CharSet(StringPiece characters) {
  memset(bits_, 0, sizeof(bits_));
  for (size_t i = 0; i < characters.length(); ++i) {
    Add(characters[i]);
  }
}

}  // namespace strings

