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

// This defines some convenience functions for manipulating cpu_set_t
// provided by glibc.  Use this if you need to deal with syscalls which expect
// to use cpu_set_t arguments, such as sched_setaffinity().

#ifndef UTIL_OS_CORE_CPU_SET_H_
#define UTIL_OS_CORE_CPU_SET_H_

#define _GNU_SOURCE 1  // Required as per the man page for CPU_SET.
#include <sched.h>

#include <string>
using ::std::string;

#include "base/integral_types.h"
#include "base/logging.h"
#include "google/protobuf/repeated_field.h"

using ::google::protobuf::RepeatedField;

namespace util_os_core {

// Converts a 64-bit mask of CPUs to a cpu_set_t. Use of this mechanism is
// discouraged.
void UInt64ToCpuSet(uint64 cpu_mask, cpu_set_t *cpu_set);

// Converts a 64-bit mask of CPUs to a cpu_set_t and returns it. Use of this
// mechanism is discouraged.
cpu_set_t UInt64ToCpuSet(uint64 cpu_mask);

// Returns a hex string for a set. The LSB is always CPU 0.
// Args:
//   cpu_set:     the set to be converted.
//   add_prefix:  if true, add a "0x" prefix, else do not.
string CpuSetToHexString(const cpu_set_t *cpu_set, bool add_prefix);

// Returns a hex string (with "0x" prefix) for a set. The LSB is always CPU 0.
inline string CpuSetToHexString(const cpu_set_t *cpu_set) {
  return CpuSetToHexString(cpu_set, true);
}

// Returns a hex string (with "0x" prefix) for a set. The LSB is always CPU 0.
// C++ friendly idiom (allows passing a function with a cpu_set_t return value).
inline string CpuSetToHexString(const cpu_set_t &cpu_set) {
  return CpuSetToHexString(&cpu_set);
}

// Sets the current set from a hex string (with or without prefix). The LSB
// is assumed to be CPU 0. Returns false if the string does not parse.
bool HexStringToCpuSet(const string &in_str, cpu_set_t *cpu_set);

// Converts a hex string (with or without prefix) to a cpu_set_t and returns the
// result. The LSB is assumed to be CPU 0. Generates a fatal log error if the
// string does not parse.
cpu_set_t HexStringToCpuSet(const string &in_str);

// Clear all the bits in a cpu_set_t. This is a type-safe wrapper around
// CPU_ZERO.
inline void CpuSetClear(cpu_set_t *cpu_set) {
  CPU_ZERO(cpu_set);
}

// Clear a subset of bits from a cpu_set_t.
// Args:
//   in:        the set to be cleared.
//   to_clear:  the set to clear from "in".
//   result:    the result set. This may be the same as "in".
//   cleared:   the set of bits that were cleared in "in". This may be NULL.
void CpuSetClearSubset(const cpu_set_t *in, const cpu_set_t *to_clear,
                       cpu_set_t *result, cpu_set_t *cleared);

// Clear a subset of bits from a cpu_set_t.
// Args:
//   in:        the set to be cleared.
//   to_clear:  the set to clear from "in".
//   result:    the result set. This may be the same as "in".
inline void CpuSetClearSubset(const cpu_set_t *in, const cpu_set_t *to_clear,
                              cpu_set_t *result) {
  CpuSetClearSubset(in, to_clear, result, NULL);
}

// Clear a specified bit in a cpu_set_t. This is a type-safe wrapper around
// CPU_CLR.
inline void CpuSetRemove(int cpu_id, cpu_set_t *cpu_set) {
  CHECK_GE(cpu_id, 0)           << "cpu_id is negative";
  CHECK_LT(cpu_id, CPU_SETSIZE) << "cpu_id is too large";
  CPU_CLR(cpu_id, cpu_set);
}

// Set a specified bit in a cpu_set_t. This is a type-safe wrapper around
// CPU_SET.
inline void CpuSetInsert(int cpu_id, cpu_set_t *cpu_set) {
  CHECK_GE(cpu_id, 0)           << "cpu_id is negative";
  CHECK_LT(cpu_id, CPU_SETSIZE) << "cpu_id is too large";
  CPU_SET(cpu_id, cpu_set);
}

// Test if a specified bit in a cpu_set_t is set. This is a type-safe wrapper
// around CPU_ISSET.
inline bool CpuSetContains(int cpu_id, const cpu_set_t *cpu_set) {
  CHECK_GE(cpu_id, 0)           << "cpu_id is negative";
  CHECK_LT(cpu_id, CPU_SETSIZE) << "cpu_id is too large";
  return CPU_ISSET(cpu_id, cpu_set) ? true : false;
}

// C++ friendly idiom (allows passing a function with a cpu_set_t return value).
inline bool CpuSetContains(int cpu_id, const cpu_set_t &cpu_set) {
  return CpuSetContains(cpu_id, &cpu_set);
}

// Count the number of bits set in a cpu_set_t. This is a type-safe wrapper
// around CPU_COUNT.
inline int CpuSetCountCpus(const cpu_set_t *cpu_set) {
  return CPU_COUNT(cpu_set);
}

// C++ friendly idiom (allows passing a function with a cpu_set_t return value).
inline int CpuSetCountCpus(const cpu_set_t &cpu_set) {
  return CpuSetCountCpus(&cpu_set);
}

// Compute logical AND of two cpu_set_t objects. This is a type-safe wrapper
// around CPU_AND.
inline void CpuSetAnd(cpu_set_t *dest, const cpu_set_t *src1,
                      const cpu_set_t *src2) {
  CPU_AND(dest, src1, src2);
}

// Compute logical OR of two cpu_set_t objects. This is a type-safe wrapper
// around CPU_OR.
inline void CpuSetOr(cpu_set_t *dest, const cpu_set_t *src1,
                     const cpu_set_t *src2) {
  CPU_OR(dest, src1, src2);
}

// Compute logical XOR of two cpu_set_t objects. This is a type-safe wrapper
// around CPU_XOR.
inline void CpuSetXor(cpu_set_t *dest, const cpu_set_t *src1,
                      const cpu_set_t *src2) {
  CPU_XOR(dest, src1, src2);
}

// Test if a cpu_set_t is empty (all zeros).
// Returns true if empty, else false.
bool CpuSetTestEmpty(const cpu_set_t *cpu_set);

// C++ friendly idiom (allows passing a function with a cpu_set_t return value).
inline bool CpuSetTestEmpty(const cpu_set_t &cpu_set) {
  return CpuSetTestEmpty(&cpu_set);
}

// Compare if one cpu_set_t is equal to another. This is a type-safe wrapper
// around CPU_EQUAL.
// Returns true if equal, else false.
inline bool CpuSetTestEqual(const cpu_set_t *lhs, const cpu_set_t *rhs) {
  return CPU_EQUAL(lhs, rhs) ? true : false;
}

// C++ friendly idiom (allows passing a function with a cpu_set_t return value).
inline bool CpuSetTestEqual(const cpu_set_t &lhs, const cpu_set_t &rhs) {
  return CpuSetTestEqual(&lhs, &rhs);
}

// Compare if one cpu_set_t is less than another.
// Returns:
//   -1: lhs <  rhs
//    0: lhs == rhs
//    1: lhs >  rhs
int CpuSetCompare(const cpu_set_t *lhs, const cpu_set_t *rhs);

// Functor for CpuSetIsLessThan() that can be used for constructing sets of
// cpu_set_t elements.
struct CpuSetLessThan {
  bool operator()(const cpu_set_t &lhs, const cpu_set_t &rhs) const {
    return CpuSetCompare(&lhs, &rhs) < 0;
  }
};

// Function to return an empty (cleared) cpu_set_t. This function has no
// side-effects and returns Plain Old Data, so it is safe to use to initialise
// a static or global variable.
cpu_set_t CpuSetMakeEmpty();

// Function to get a cpu_set_t from a repeated uint64 protobuf field.
// If the repeated field is empty (size() == 0) the cpu_set_t will be cleared.
// The last uint64 field will be written to the least significant sub part of
// the cpu_set_t.
cpu_set_t ProtobufToCpuSet(const RepeatedField<uint64> &pb);

// Function to write a cpu_set_t to a repeated uint64 protobuf field. If the
// cpu_set_t is empty (no bits set) then nothing will be written. The least
// significant sub part of the cpu_set_t will be written last.
void CpuSetToProtobuf(const cpu_set_t& cpu_set, RepeatedField<uint64>* pb);

}  // namespace util_os_core

// Logging helper for printing cpu_set_t objects. This is nice in general, and
// is very nice for objects in STL containers. This has to be defined in the
// outer namespace (where cpu_set_t is defined). Blech.
::std::ostream& operator<<(::std::ostream& o, const cpu_set_t &cpu_set);

#endif  // UTIL_OS_CORE_CPU_SET_H_
