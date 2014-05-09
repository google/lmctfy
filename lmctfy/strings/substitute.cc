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

#include "strings/substitute.h"

#include "base/logging.h"
#include "base/macros.h"
#include "strings/ascii_ctype.h"
#include "strings/escaping.h"
#include "util/gtl/stl_util.h"

namespace strings {

using internal::SubstituteArg;

const SubstituteArg SubstituteArg::NoArg;

// Returns the number of args in arg_array which were passed explicitly
// to Substitute().
static int CountSubstituteArgs(const SubstituteArg* const* args_array) {
  int count = 0;
  while (args_array[count] != &SubstituteArg::NoArg) {
    ++count;
  }
  return count;
}

void SubstituteAndAppend(
    string* output, StringPiece format,
    const SubstituteArg& arg0, const SubstituteArg& arg1,
    const SubstituteArg& arg2, const SubstituteArg& arg3,
    const SubstituteArg& arg4, const SubstituteArg& arg5,
    const SubstituteArg& arg6, const SubstituteArg& arg7,
    const SubstituteArg& arg8, const SubstituteArg& arg9) {
  const SubstituteArg* const args_array[] = {
    &arg0, &arg1, &arg2, &arg3, &arg4, &arg5, &arg6, &arg7, &arg8, &arg9, NULL
  };

  // Determine total size needed.
  int size = 0;
  for (int i = 0; i < format.size(); i++) {
    if (format[i] == '$') {
      if (i+1 >= format.size()) {
        LOG(DFATAL) << "Invalid strings::Substitute() format string: \""
                    << CEscape(format) << "\".";
        return;
      } else if (ascii_isdigit(format[i+1])) {
        int index = format[i+1] - '0';
        if (args_array[index]->size() == -1) {
          LOG(DFATAL)
            << "strings::Substitute format string invalid: asked for \"$"
            << index << "\", but only " << CountSubstituteArgs(args_array)
            << " args were given.  Full format string was: \""
            << CEscape(format) << "\".";
          return;
        }
        size += args_array[index]->size();
        ++i;  // Skip next char.
      } else if (format[i+1] == '$') {
        ++size;
        ++i;  // Skip next char.
      } else {
        LOG(DFATAL) << "Invalid strings::Substitute() format string: \""
                    << CEscape(format) << "\".";
        return;
      }
    } else {
      ++size;
    }
  }

  if (size == 0) return;

  // Build the string.
  int original_size = output->size();
  STLStringResizeUninitialized(output, original_size + size);
  char* target = string_as_array(output) + original_size;
  for (int i = 0; i < format.size(); i++) {
    if (format[i] == '$') {
      if (ascii_isdigit(format[i+1])) {
        const SubstituteArg* src = args_array[format[i+1] - '0'];
        memcpy(target, src->data(), src->size());
        target += src->size();
        ++i;  // Skip next char.
      } else if (format[i+1] == '$') {
        *target++ = '$';
        ++i;  // Skip next char.
      }
    } else {
      *target++ = format[i];
    }
  }

  DCHECK_EQ(target - output->data(), output->size());
}

SubstituteArg::SubstituteArg(const void* value) {
  COMPILE_ASSERT(sizeof(scratch_) >= sizeof(value) * 2 + 2,
                 fix_sizeof_scratch_);
  if (value == NULL) {
    text_ = "NULL";
    size_ = strlen(text_);
  } else {
    char* ptr = scratch_ + sizeof(scratch_);
    uintptr_t num = reinterpret_cast<uintptr_t>(value);
    static const char kHexDigits[] = "0123456789abcdef";
    do {
      *--ptr = kHexDigits[num & 0xf];
      num >>= 4;
    } while (num != 0);
    *--ptr = 'x';
    *--ptr = '0';
    text_ = ptr;
    size_ = scratch_ + sizeof(scratch_) - ptr;
  }
}

}  // namespace strings

