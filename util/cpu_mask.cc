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

// Implementation of the CpuMask class, which encapsulates the lower-level
// cpu_set_t.

#include "util/cpu_mask.h"

#include <ctype.h>
#include <string.h>

#include "base/integral_types.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "strings/stringpiece.h"

namespace util {

static CpuMask *ComputeCpuMaskAll() {
  CpuMask *cpu_mask = new CpuMask();
  for (int cpu_id = 0; cpu_id < CPU_SETSIZE; ++cpu_id) {
    cpu_mask->Set(cpu_id);
  }
  return cpu_mask;
}

// Return a CpuMask object which has all bits set.
static const CpuMask &AllCpus() {
  static CpuMask *cpu_mask_all = ComputeCpuMaskAll();
  return *cpu_mask_all;
}

// Constructors.

CpuMask::CpuMask() {
  Clear();
}

CpuMask::CpuMask(uint64 init_mask) {
  int max_cpus = sizeof(init_mask) * 8;

  Clear();
  for (int cpu_id = 0; init_mask != 0 && cpu_id < max_cpus; ++cpu_id) {
    if (init_mask & 1) {
      Set(cpu_id);
    }
    init_mask = init_mask >> 1;
  }
}

CpuMask::CpuMask(const cpu_set_t &init_set) {
  cpu_set_ = init_set;
}

CpuMask::CpuMask(const google::protobuf::RepeatedField<uint64_t> &init_pb) {
  Clear();
  int pb_max = init_pb.size() - 1;
  for (int pb_index = pb_max; pb_index >= 0; --pb_index) {
    uint64 sub_mask = init_pb.Get(pb_index);
    for (int sub_id = 0; sub_mask; ++sub_id) {
      if (sub_mask & 1) {
        Set(sub_id + 64 * (pb_max - pb_index));
      }
      sub_mask >>= 1;
    }
  }
}

// Exporting member functions (converting to other formats).

string CpuMask::ToHexString() const {
  // Accumulate a vector of bytes holding the CPU bitmask.
  vector<uint8> bytes(1, 0);

  // How many CPUs do we need to find?
  int num_cpus_remaining = CountCpus();

  // For each byte...
  for (int i = 0; num_cpus_remaining != 0 && i < CPU_SETSIZE; i += 8) {
    // For each bit...
    for (int j = 0; j < 8; ++j) {
      if (IsSet(i + j)) {
        bytes.back() |= (1 << j);
        num_cpus_remaining--;
      }
    }
    // If we have more CPUs left, go again.
    if (num_cpus_remaining != 0) {
      bytes.push_back(0);
    }
  }

  // Turn the result into a hex string.
  string result;
  for (int b = bytes.size() - 1; b >= 0; --b) {
    result += StringPrintf("%02x", bytes[b]);
  }
  // Strip leading '0' if it exists, to make the result more consistent with
  // standard hex formatting.
  if (result[0] == '0') {
    result = result.substr(1, result.length() - 1);
  }
  return "0x" + result;
}

void CpuMask::WriteToProtobuf(google::protobuf::RepeatedField<uint64_t> *pb)
    const {
  pb->Clear();
  uint64 sub_mask = 0;
  int bit_count = 0;
  bool found_non_zero_sub_mask = false;
  for (int cpu_id = CPU_SETSIZE - 1; cpu_id >= 0; --cpu_id) {
    if (IsSet(cpu_id)) {
      sub_mask |= 1;
    }
    if (++bit_count == 64) {
      bit_count = 0;
      if (sub_mask || found_non_zero_sub_mask) {
        pb->Add(sub_mask);
        found_non_zero_sub_mask = true;
      }
      sub_mask = 0;
    }
    sub_mask <<= 1;
  }
}

// Member functions.

bool CpuMask::FromHexString(StringPiece hex_str) {
  Clear();

  // Chop off the leading "0x" if present.
  int pos = 0;
  if (hex_str.size() >= 2 && hex_str[0] == '0' && tolower(hex_str[1]) == 'x') {
    pos = 2;
  }
  StringPiece str(hex_str, pos);

  if (str.empty()) {
    LOG(WARNING) << "Invalid hex string: \"" << hex_str.ToString() << "\"";
    return false;
  }

  // For each hex digit...
  int i = 0;
  for (StringPiece::reverse_iterator it = str.rbegin();
       it != str.rend();
       ++it, ++i) {
    if (!isxdigit(*it)) {
      LOG(WARNING) << "Invalid hex string: \"" << hex_str.ToString() << "\"";
      return false;
    }

    uint8 hexit = tolower(*it);

    uint8 val;
    if (hexit >= '0' && hexit <= '9') {
      val = hexit - '0';
    } else if (hexit >= 'a' && hexit <= 'f') {
      val = 10 + hexit - 'a';
    } else {
      LOG(WARNING)
          << "isxdigit() but not [0-9a-fA-F]: this should never happen";
      return false;
    }

    for (int j = 0; val != 0 && j < 4; ++j) {
      uint8 m = 1 << j;
      if (val & m) {
        Set((i * 4) + j);
        val &= ~m;
      }
    }
  }
  return true;
}

void CpuMask::ClearSubset(const CpuMask &to_clear, CpuMask *cleared) {
  if (cleared) *cleared = *this & to_clear;
  CpuMask cpu_mask_tmp = AllCpus() ^ to_clear;
  *this &= cpu_mask_tmp;
}

bool CpuMask::IsEmpty() const {
  // Profiling has found that it's faster to use CPU_COUNT() rather than using a
  // loop that tries to be efficient by returning as soon as any CPU is found.
  // TODO(rgooch): if this function turns up in profiling, consider an
  //               optimisation which caches the "is cleared" state.
  return CPU_COUNT(&cpu_set_) == 0 ? true : false;
}

int CpuMask::Compare(const CpuMask &other) const {
  if (*this == other) {
    return 0;
  }
  for (int cpu_id = CPU_SETSIZE - 1; cpu_id >= 0; --cpu_id) {
    bool lhs_bit = IsSet(cpu_id) ? 1 : 0;
    bool rhs_bit = other.IsSet(cpu_id) ? 1 : 0;
    if (!lhs_bit && rhs_bit) {
      return -1;
    }
    if (lhs_bit && !rhs_bit) {
      return 1;
    }
  }
  LOG(FATAL) << "lhs == rhs but specialised test missed";
}

bool CpuMask::operator<(const CpuMask &other) const {
  return Compare(other) < 0;
}

ostream& operator<<(ostream& o, const CpuMask &cpu_mask) {
  return o << cpu_mask.ToHexString();
}

}  // namespace util
