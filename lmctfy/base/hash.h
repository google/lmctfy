// Copyright 2012 Google Inc. All Rights Reserved.
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
//
// The file provides hash/fingerprinting functionalities.

//
#ifndef UTIL_HASH_HASH_H_
#define UTIL_HASH_HASH_H_

#include <stddef.h>
#include <string.h>

#include <string>
using std::string;

#include <ext/hash_map>
using __gnu_cxx::hash;
using __gnu_cxx::hash_map;
#include <ext/hash_set>
using __gnu_cxx::hash;
using __gnu_cxx::hash_set;
using __gnu_cxx::hash_multiset;

#include "base/integral_types.h"
#include "base/port.h"

// --------------- Hashing -----------------------------------------------------

uint32 Hash32StringWithSeedReferenceImplementation(const char *s, uint32 len,
                                                   uint32 seed);

uint32 Hash32StringWithSeed(const char *s, uint32 len, uint32 c);

uint64 Hash64StringWithSeed(const char *s, uint32 len, uint64 seed);

template<typename T>
inline uint64 Hash64NumWithSeed(T num, uint64 seed) {
  return Hash64StringWithSeed(reinterpret_cast<const char*>(&num),
                              sizeof(num), seed);
}
inline uint64 Hash64FloatWithSeed(float num, uint64 seed) {
  return Hash64StringWithSeed(reinterpret_cast<const char*>(&num),
                              sizeof(num), seed);
}
inline uint64 Hash64DoubleWithSeed(double num, uint64 seed) {
  return Hash64StringWithSeed(reinterpret_cast<const char*>(&num),
                              sizeof(num), seed);
}

namespace hash_internal {

// We have some special cases for 64-bit hardware and x86-64 in particular.
// Instead of sprinkling ifdefs through the file, we have one ugly ifdef here.
// Later code can then use "if" instead of "ifdef".
#if defined(__x86_64__)
enum { x86_64 = true, sixty_four_bit = true };
#elif defined(_LP64)
enum { x86_64 = false, sixty_four_bit = true };
#else
enum { x86_64 = false, sixty_four_bit = false };
#endif

// Arbitrary mix constants.
static const uint32 kMix32 = 0xdfdb04fcUL;
static const uint64 kMix64 = GG_ULONGLONG(0x92c3575458ddc83f);

}  // namespace hash_internal

inline size_t HashStringThoroughly(const char* s, size_t len) {
  if (hash_internal::sixty_four_bit) {
    return Hash64StringWithSeed(s, static_cast<uint32>(len),
                                hash_internal::kMix64);
  }
  return static_cast<size_t>(Hash32StringWithSeed(s, static_cast<uint32>(len),
                             hash_internal::kMix32));
}

inline size_t HashTo32(const char* s, size_t len) {
  return Hash32StringWithSeed(s, static_cast<uint32>(len),
                              hash_internal::kMix32);
}

// --------------- STL hashers -------------------------------------------------

#include <ext/hash_set>
namespace __gnu_cxx {


// STLport and MSVC 10.0 above already define these.
#if !defined(_STLP_LONG_LONG) && !(defined(_MSC_VER) && _MSC_VER >= 1600)

#if defined(_MSC_VER)
// MSVC's stl implementation with _MSC_VER less than 1600 doesn't have
// this hash struct. STLport already defines this.
template <typename T>
struct hash {
  size_t operator()(const T& t) const;
};
#endif  // defined(_MSC_VER)

template<> struct hash<int64> {
  size_t operator()(int64 x) const { return static_cast<size_t>(x); }
};

template<> struct hash<uint64> {
  size_t operator()(uint64 x) const { return static_cast<size_t>(x); }
};

#endif  // !defined(_STLP_LONG_LONG) && !(defined(_MSC_VER) && _MSC_VER >= 1600)

template<> struct hash<bool> {
  size_t operator()(bool x) const { return static_cast<size_t>(x); }
};

#if defined(__GNUC__)
// Use our nice hash function for strings
template<class _CharT, class _Traits, class _Alloc>
struct hash<std::basic_string<_CharT, _Traits, _Alloc> > {
  size_t operator()(const std::basic_string<_CharT, _Traits, _Alloc>& k) const {
    return HashTo32(k.data(), static_cast<uint32>(k.length()));
  }
};

// they don't define a hash for const string at all
template<> struct hash<const std::string> {
  size_t operator()(const std::string& k) const {
    return HashTo32(k.data(), static_cast<uint32>(k.length()));
  }
};
#endif  // defined(__GNUC__)


}  // namespace __gnu_cxx


// --------------- Fingerprints ------------------------------------------------

uint64 Fingerprint(const char *s, uint32 len);

template<typename T>
inline uint64 Fingerprint(T num) {
  return Fingerprint(reinterpret_cast<const char*>(&num), sizeof(num));
}

#endif  // UTIL_HASH_HASH_H_
