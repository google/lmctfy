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

// This defines a class to encapsulate the cpu_set_t type and the CPU_*
// functions that operate on it. It is meant as a drop-in replacement for an
// integer type that is limited to bitwise logical operations and comparisons.
//
// CpuMask objects may be converted to/from a repeated uint64 protobuf field.
// The uint64 at the end (highest index) of the repeated field corresponds to
// the least significant sub part of the CpuMask. An empty field (size() == 0)
// corresponds to an empty CpuMask.
//
// Given a protobuf such as:
//   message MyProto {
//     repeated uint64 cpu_mask = 1;
//   }
//
// you can create a CpuMask from the protobuf as follows:
//
//   MyProto pb = SomeSourceFunction();
//   CpuMask a_new_mask(pb.cpu_mask());
//
// and you can write a CpuMask to the protobuf as follows:
//
//    CpuMask cpu_mask = AnotherSourceFunction();
//    MyProto pb;
//    cpu_mask.WriteToProtobuf(pb.mutable_cpu_mask());

#ifndef UTIL_CPU_MASK_H_
#define UTIL_CPU_MASK_H_

#define _GNU_SOURCE 1  // Required as per the man page for CPU_SET.
#include <sched.h>

#include <string>

#include "base/integral_types.h"
#include "base/logging.h"
#include "google/protobuf/repeated_field.h"
#include "strings/stringpiece.h"

namespace util {

// This class encapsulates a cpu_set_t (which represents a set of CPUs) and
// provides overloading of key operators (bitwise logical, compare). CPU numbers
// are zero-based and contiguous.
class CpuMask {
 public:
  // Constructors.

  // Default constructor: initialises to empty (all zeros).
  // The default copy constructor is intended.
  CpuMask();

  // Initialise using a 64-bit mask of CPUs. Use of this constructor is
  // discouraged.
  explicit CpuMask(uint64 init_mask);

  // Initialise using a cpu_set_t.
  explicit CpuMask(const cpu_set_t &init_set);

  // Initialise using a repeated uint64 protobuf field.
  explicit CpuMask(const google::protobuf::RepeatedField<uint64_t> &init_pb);

  // Exporting member functions (converting to other formats).

  // Gets the encapsulated cpu_set_t.
  cpu_set_t ToCpuSet() const { return cpu_set_; }

  // Returns a hex string (with "0x" prefix). The LSB is always CPU 0.
  string ToHexString() const;

  // Writes to a repeated uint64 protobuf field.
  void WriteToProtobuf(google::protobuf::RepeatedField<uint64_t> *pb) const;

  // General member functions.

  // Converts from a hex string (with or without "0x" prefix). The LSB is
  // assumed to be CPU 0. Returns false if the string does not parse. Use of
  // this interface is discouraged due to potential parsing errors.
  bool FromHexString(StringPiece hex_str);

  // Clears all the bits.
  void Clear() {
    CPU_ZERO(&cpu_set_);
  }

  // Clears a subset of bits.
  // Args:
  //   to_clear:  a CpuMask containing the bits to clear.
  //   cleared:   the bits that were cleared are written here.
  void ClearSubset(const CpuMask &to_clear, CpuMask *cleared);

  // Clears a subset of bits.
  // Args:
  //   to_clear:  a CpuMask containing the bits to clear.
  void ClearSubset(const CpuMask &to_clear) {
    ClearSubset(to_clear, NULL);
  }

  // Clears a specified bit.
  void Clear(int cpu_id) {
    CHECK_GE(cpu_id, 0)           << "cpu_id is negative";
    CHECK_LT(cpu_id, CPU_SETSIZE) << "cpu_id is too large";
    CPU_CLR(cpu_id, &cpu_set_);
  }

  // Sets a specified bit.
  void Set(int cpu_id) {
    CHECK_GE(cpu_id, 0)           << "cpu_id is negative";
    CHECK_LT(cpu_id, CPU_SETSIZE) << "cpu_id is too large";
    CPU_SET(cpu_id, &cpu_set_);
  }

  // Tests if a specified bit is set.
  bool IsSet(int cpu_id) const {
    CHECK_GE(cpu_id, 0)           << "cpu_id is negative";
    CHECK_LT(cpu_id, CPU_SETSIZE) << "cpu_id is too large";
    return CPU_ISSET(cpu_id, &cpu_set_) ? true : false;
  }

  // Counts the number of bits set.
  int CountCpus() const {
    return CPU_COUNT(&cpu_set_);
  }

  // AND operator (this & other).
  CpuMask operator&(const CpuMask &other) const {
    cpu_set_t result;
    CPU_AND(&result, &cpu_set_, &other.cpu_set_);
    return CpuMask(result);
  }

  // AND/ASSIGN operator (this &= other).
  CpuMask &operator&=(const CpuMask &other) {
    CPU_AND(&cpu_set_, &cpu_set_, &other.cpu_set_);
    return *this;
  }

  // OR operator (this | other).
  CpuMask operator|(const CpuMask &other) const {
    cpu_set_t result;
    CPU_OR(&result, &cpu_set_, &other.cpu_set_);
    return CpuMask(result);
  }

  // OR/ASSIGN operator (this |= other).
  CpuMask &operator|=(const CpuMask &other) {
    CPU_OR(&cpu_set_, &cpu_set_, &other.cpu_set_);
    return *this;
  }

  // XOR operator (this ^ other).
  CpuMask operator^(const CpuMask &other) const {
    cpu_set_t result;
    CPU_XOR(&result, &cpu_set_, &other.cpu_set_);
    return CpuMask(result);
  }

  // XOR/ASSIGN operator (this ^= other).
  CpuMask &operator^=(const CpuMask &other) {
    CPU_XOR(&cpu_set_, &cpu_set_, &other.cpu_set_);
    return *this;
  }

  // Tests if empty (all zeros).
  // Returns true if empty, else false.
  bool IsEmpty() const;

  // Equality operator (this == other).
  bool operator==(const CpuMask &other) const {
    return CPU_EQUAL(&cpu_set_, &other.cpu_set_) ? true : false;
  }

  // Inequality operator (this != other).
  bool operator!=(const CpuMask &other) const {
    return CPU_EQUAL(&cpu_set_, &other.cpu_set_) ? false : true;
  }

  // Compares if one CpuMask is less than another.
  // Returns:
  //   -1: this <  other
  //    0: this == other
  //    1: this >  other
  int Compare(const CpuMask &other) const;

  // Less than operator (this < other).
  bool operator<(const CpuMask &other) const;

 private:
  cpu_set_t cpu_set_;
};

// Logging helper for printing CpuMask objects. This is nice in general, and
// is very nice for objects in STL containers.
ostream& operator<<(ostream& o, const CpuMask &cpu_mask);

}  // namespace util

#endif  // UTIL_CPU_MASK_H_
