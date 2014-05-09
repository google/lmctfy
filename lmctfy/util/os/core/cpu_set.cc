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

// Implementation of the convenience functions for manipulating cpu_set_t
// provided by glibc.

#include "util/os/core/cpu_set.h"

#include <ctype.h>
#include <string.h>
#include "base/integral_types.h"
#include "base/logging.h"
#include "base/stringprintf.h"

namespace util_os_core {

static cpu_set_t compute_cpu_set_all() {
  cpu_set_t cpu_set;
  for (int cpu_id = 0; cpu_id < CPU_SETSIZE; ++cpu_id)
    CPU_SET(cpu_id, &cpu_set);
  return cpu_set;
}

static const cpu_set_t cpu_set_all = compute_cpu_set_all();


void UInt64ToCpuSet(uint64 cpu_mask, cpu_set_t *cpu_set) {
  int max_cpus = sizeof(cpu_mask) * 8;

  CPU_ZERO(cpu_set);
  for (int cpu_id = 0; cpu_mask != 0 && cpu_id < max_cpus; ++cpu_id) {
    if (cpu_mask & 1) {
      CPU_SET(cpu_id, cpu_set);
    }
    cpu_mask = cpu_mask >> 1;
  }
}

cpu_set_t UInt64ToCpuSet(uint64 cpu_mask) {
  cpu_set_t cpu_set;
  UInt64ToCpuSet(cpu_mask, &cpu_set);
  return cpu_set;
}

string CpuSetToHexString(const cpu_set_t *cpu_set, bool add_prefix) {
  // Accumulate a vector of bytes holding the CPU bitmask.
  vector<uint8> bytes(1, 0);

  // How many CPUs do we need to find?
  int num_cpus_remaining = CPU_COUNT(cpu_set);

  // For each byte...
  for (int i = 0; num_cpus_remaining != 0 && i < CPU_SETSIZE; i += 8) {
    // For each bit...
    for (int j = 0; j < 8; ++j) {
      if (CPU_ISSET(i+j, cpu_set)) {
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
  return add_prefix ? "0x" + result : result;
}

bool HexStringToCpuSet(const string &in_str, cpu_set_t *cpu_set) {
  cpu_set_t tmp;
  CPU_ZERO(&tmp);

  // Make a local string, since we may need to chop the prefix off of in_str,
  // and in_str is const.
  string str;

  // Chop off the leading "0x" if present.
  if (in_str.size() >= 2 && in_str[0] == '0' && tolower(in_str[1]) == 'x') {
    str = in_str.substr(2, string::npos);
  } else {
    str = in_str;
  }

  if (str.empty()) {
    return false;
  }

  // For each hex digit...
  int i = 0;
  for (string::reverse_iterator it = str.rbegin();
       it != str.rend();
       ++it, ++i) {
    if (!isxdigit(*it)) {
      return false;
    }

    uint8 hexit = tolower(*it);

    uint8 val;
    if (hexit >= '0' && hexit <= '9') {
      val = hexit - '0';
    } else if (hexit >= 'a' && hexit <= 'f') {
      val = 10 + hexit - 'a';
    } else {
      LOG(FATAL) << "isxdigit() but not [0-9a-fA-F]: this should never happen";
    }

    for (int j = 0; val != 0 && j < 4; ++j) {
      uint8 m = 1 << j;
      if (val & m) {
        CPU_SET((i*4) + j, &tmp);
        val &= ~m;
      }
    }
  }
  memcpy(cpu_set, &tmp, sizeof(*cpu_set));
  return true;
}

cpu_set_t HexStringToCpuSet(const string &in_str) {
  cpu_set_t cpu_set;
  if (!HexStringToCpuSet(in_str, &cpu_set)) {
    LOG(FATAL) << "Cannot parse hex string: " << in_str;
  }
  return cpu_set;
}

void CpuSetClearSubset(const cpu_set_t *in, const cpu_set_t *to_clear,
                       cpu_set_t *result, cpu_set_t *cleared) {
  if (cleared) CpuSetAnd(cleared, in, to_clear);
  cpu_set_t cpu_set_tmp;
  CpuSetXor(&cpu_set_tmp, &cpu_set_all, to_clear);
  CpuSetAnd(result, in, &cpu_set_tmp);
}

bool CpuSetTestEmpty(const cpu_set_t *cpu_set) {
  // Profiling has found that it's faster to use CPU_COUNT() rather than using a
  // loop that tries to be efficient by returning as soon as any CPU is found.
  return CPU_COUNT(cpu_set) == 0 ? true : false;
}

int CpuSetCompare(const cpu_set_t *lhs, const cpu_set_t *rhs) {
  if (CpuSetTestEqual(lhs, rhs)) {
    return 0;
  }
  for (int cpu_id = CPU_SETSIZE - 1; cpu_id >= 0; --cpu_id) {
    bool lhs_bit = CPU_ISSET(cpu_id, lhs) ? 1 : 0;
    bool rhs_bit = CPU_ISSET(cpu_id, rhs) ? 1 : 0;
    if (!lhs_bit && rhs_bit) {
      return -1;
    }
    if (lhs_bit && !rhs_bit) {
      return 1;
    }
  }
  LOG(FATAL) << "lhs == rhs but specialised test missed";
}

cpu_set_t CpuSetMakeEmpty() {
  cpu_set_t cpu_set;
  CpuSetClear(&cpu_set);
  return cpu_set;
}

cpu_set_t ProtobufToCpuSet(const RepeatedField<uint64> &pb) {
  cpu_set_t cpu_set;
  CpuSetClear(&cpu_set);
  for (int index = pb.size() - 1; index >= 0; --index) {
    uint64 sub_mask = pb.Get(index);
    for (int sub_id = 0; sub_mask; ++sub_id) {
      if (sub_mask & 1) {
        CpuSetInsert(sub_id + 64 * index, &cpu_set);
      }
      sub_mask >>= 1;
    }
  }
  return cpu_set;
}

void CpuSetToProtobuf(const cpu_set_t &cpu_set, RepeatedField<uint64> *pb) {
  pb->Clear();
  uint64 sub_mask = 0;
  int bit_count = 0;
  bool found_non_zero_sub_mask = false;
  for (int cpu_id = CPU_SETSIZE - 1; cpu_id >= 0; --cpu_id) {
    if (CpuSetContains(cpu_id, cpu_set)) {
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

}  // namespace util_os_core

::std::ostream& operator<<(::std::ostream& o, const cpu_set_t &cpu_set) {
  o << util_os_core::CpuSetToHexString(&cpu_set);
  return o;
}
