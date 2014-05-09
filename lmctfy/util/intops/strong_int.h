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

// StrongInt<T> is a simple template class mechanism for defining "logical"
// integer-like class types that support almost all of the same functionality
// as native integer types, but which prevents assignment, construction, and
// other operations from other integer-like types.  In other words, you cannot
// assign from raw integer types or other StrongInt<> types, nor can you do
// most arithemtic or logical operations.  This provides a simple form of
// dimensionality in that you can add two instances of StrongInt<T>, producing
// a StrongInt<T>, but you can not add a StrongInt<T> and a raw T nor can you
// add a StrongInt<T> and a StrongInt<U>.  Details on supported operations are
// below.
//
// In addition to type strength, StrongInt provides a way to inject (optional)
// validation of the various operations.  This allows you to define StrongInt
// types that check for overflow conditions and react in standard or custom
// ways.
//
// A StrongInt<T> with a NullStrongIntValidator should compile away to a raw T
// in optimized mode.  What this means is that the generated assembly for:
//
//   int64 foo = 123;
//   int64 bar = 456;
//   int64 baz = foo + bar;
//
// ...should be identical to the generated assembly for:
//
//    DEFINE_STRONG_INT_TYPE(MyStrongInt, int64);
//    MyStrongInt foo(123);
//    MyStrongInt bar(456);
//    MyStrongInt baz = foo + bar;
//
// Since the methods are all inline and non-virtual and the class has just
// one data member, the compiler can erase the StrongInt class entirely in its
// code-generation phase.  This also means that you can pass StrongInt<T>
// around by value just as you would a raw T.
//
// Usage:
//   DEFINE_STRONG_INT_TYPE(Name, NativeType);
//
//     Defines a new StrongInt type named 'Name' in the current namespace with
//     no validation of operations.
//
//     Name: The desired name for the new StrongInt typedef.  Must be unique
//         within the current namespace.
//     NativeType: The primitive integral type this StrongInt will hold, as
//         defined by base::is_integral (see base/type_traits.h).
//
//  StrongInt<TagType, NativeType, ValidatorType = NullStrongIntValidator>
//
//    Creates a new StrongInt instance directly.
//
//     TagType: The unique type which discriminates this StrongInt<T> from
//         other StrongInt<U> types.
//     NativeType: The primitive integral type this StrongInt will hold, as
//         defined by base::is_integral (see base/type_traits.h).
//     ValidatorType: The type of validation used by this StrongInt type.  A
//         few pre-built validator types are provided here, but the caller can
//         define any custom validator they desire.
//
// Supported operations:
//     StrongInt<T> = StrongInt<T>
//     !StrongInt<T> => bool
//     ~StrongInt<T> => StrongInt<T>
//     -StrongInt<T> => StrongInt<T>
//     +StrongInt<T> => StrongInt<T>
//     ++StrongInt<T> => StrongInt<T>
//     StrongInt<T>++ => StrongInt<T>
//     --StrongInt<T> => StrongInt<T>
//     StrongInt<T>-- => StrongInt<T>
//     StrongInt<T> + StrongInt<T> => StrongInt<T>
//     StrongInt<T> - StrongInt<T> => StrongInt<T>
//     StrongInt<T> * (numeric type) => StrongInt<T>
//     StrongInt<T> / (numeric type) => StrongInt<T>
//     StrongInt<T> % (numeric type) => StrongInt<T>
//     StrongInt<T> << (numeric type) => StrongInt<T>
//     StrongInt<T> >> (numeric type) => StrongInt<T>
//     StrongInt<T> & StrongInt<T> => StrongInt<T>
//     StrongInt<T> | StrongInt<T> => StrongInt<T>
//     StrongInt<T> ^ StrongInt<T> => StrongInt<T>
//
//   For binary operations, the equivalent op-equal (eg += vs. +) operations are
//   also supported.  Other operator combinations should cause compile-time
//   errors.
//
//   This class also provides a .value() accessor method and defines a hash
//   functor that allows the IntType to be used as key to hashable containers
//   such as hash_map and hash_set.
//
// Validators:
//   NullStrongIntValidator: Do no validation.  This should be entirely
//       optimized away by the compiler.

#ifndef UTIL_INTOPS_STRONG_INT_H_
#define UTIL_INTOPS_STRONG_INT_H_

#include <hash_map>
#include <iosfwd>
#include <limits>
#include <ostream>                                // NOLINT(readability/streams)

#include "base/macros.h"
#include "base/port.h"
#include "base/type_traits.h"

namespace util_intops {

// Define the validators which can be plugged-in to make StrongInt resilient to
// things like overflows. This is a do-nothing implementation of the
// compile-time interface.
//
// NOTE: For all validation functions that operate on an existing StrongInt<T>,
// the type argument 'T' *must* be StrongInt<T>::ValueType (the int type being
// strengthened).
//
// TODO(thockin): If we made these return the result of the operation, we could
//     implement things like ints that saturate, rather than overflow.  if
//     someone has a good use case this should be a pretty simple change.
struct NullStrongIntValidator {
  // Verify initialization of StrongInt<T> from arg, type U.
  template<typename T, typename U>
  static void ValidateInit(U arg) { /* do nothing */ }
  // Verify -value.
  template<typename T>
  static void ValidateNegate(T value) { /* do nothing */ }
  // Verify ~value;
  template<typename T>
  static void ValidateBitNot(T value) { /* do nothing */ }
  // Verify lhs + rhs.
  template<typename T>
  static void ValidateAdd(T lhs, T rhs) { /* do nothing */ }
  // Verify lhs - rhs.
  template<typename T>
  static void ValidateSubtract(T lhs, T rhs) { /* do nothing */ }
  // Verify lhs * rhs.
  template<typename T, typename U>
  static void ValidateMultiply(T lhs, U rhs) { /* do nothing */ }
  // Verify lhs / rhs.
  template<typename T, typename U>
  static void ValidateDivide(T lhs, U rhs) { /* do nothing */ }
  // Verify lhs % rhs.
  template<typename T, typename U>
  static void ValidateModulo(T lhs, U rhs) { /* do nothing */ }
  // Verify lhs << rhs.
  template<typename T>
  static void ValidateLeftShift(T lhs, int64 rhs) { /* do nothing */ }
  // Verify lhs >> rhs.
  template<typename T>
  static void ValidateRightShift(T lhs, int64 rhs) { /* do nothing */ }
  // Verify lhs & rhs.
  template<typename T>
  static void ValidateBitAnd(T lhs, T rhs) { /* do nothing */ }
  // Verify lhs | rhs.
  template<typename T>
  static void ValidateBitOr(T lhs, T rhs) { /* do nothing */ }
  // Verify lhs ^ rhs.
  template<typename T>
  static void ValidateBitXor(T lhs, T rhs) { /* do nothing */ }
};

// Holds a google3-supported integer value (of type NativeType) and behaves as
// a NativeType by exposing assignment, unary, comparison, and arithmetic
// operators.
//
// This class is NOT thread-safe.
template<typename TagType, typename NativeType,
         typename ValidatorType = NullStrongIntValidator>
class StrongInt {
 public:
  typedef NativeType ValueType;

 public:
  // Default value initialization.
  StrongInt() : value_() {
    ValidatorType::template ValidateInit<ValueType>(NativeType());
  }

  // Explicit initialization from another StrongInt type that has an
  // implementation of:
  //
  //    ToType StrongIntConvert(FromType source, ToType*);
  //
  // This uses Argument Dependent Lookup (ADL) to find which function to
  // call.
  //
  // Example: Assume you have two StrongInt types.
  //
  //      DEFINE_STRONG_INT_TYPE(Bytes, int64);
  //      DEFINE_STRONG_INT_TYPE(Megabytes, int64);
  //
  //  If you want to be able to (explicitly) construct an instance of Bytes from
  //  an instance of Megabytes, simply define a converter function in the same
  //  namespace as either Bytes or Megabytes (or both):
  //
  //      Megabytes StrongIntConvert(Bytes arg, Megabytes* /* unused */) {
  //        return Megabytes((arg >> 20).value());
  //      };
  //
  //  The second argument is needed to differentiate conversions, and it always
  //  passed as NULL.
  template<typename ArgTagType, typename ArgNativeType,
           typename ArgValidatorType>
  explicit StrongInt(StrongInt<ArgTagType, ArgNativeType,
                               ArgValidatorType> arg) {
    // We have to pass both the "from" type and the "to" type as args for the
    // conversions to be differentiated.  The converter can not be a template
    // because explicit template call syntax defeats ADL.
    StrongInt *dummy = NULL;
    StrongInt converted = StrongIntConvert(arg, dummy);
    value_ = converted.value();
  }

  // Explicit initialization from a numeric primitive.
  template<typename T>
  explicit StrongInt(T init_value) : value_() {
    ValidatorType::template ValidateInit<ValueType>(init_value);
    value_ = static_cast<ValueType>(init_value);
  }

  // Use the default copy constructor and destructor.

  // Assignment operator.
  StrongInt &operator=(StrongInt other) {
    ValidatorType::template ValidateInit<ValueType>(other.value());
    value_ = other.value();
    return *this;
  }

  // Accesses the raw value.
  ValueType value() const { return value_; }

  // Accesses the raw value, with cast.
  // Primarily for compatibility with int-type.h
  template <typename ValType>
  ValType value() const { return static_cast<ValType>(value_); }

  // Metadata functions.
  ValueType Max() const { return std::numeric_limits<ValueType>::max(); }
  ValueType Min() const { return std::numeric_limits<ValueType>::min(); }

  // Unary operators.
  bool operator!() const {
    return value_ == 0;
  }
  const StrongInt operator+() const {
    return StrongInt(value_);
  }
  const StrongInt operator-() const {
    ValidatorType::template ValidateNegate<ValueType>(value_);
    return StrongInt(-value_);
  }
  const StrongInt operator~() const {
    ValidatorType::template ValidateBitNot<ValueType>(value_);
    return StrongInt(ValueType(~value_));
  }

  // Increment and decrement operators.
  StrongInt &operator++() {  // ++x
    ValidatorType::template ValidateAdd<ValueType>(value_, ValueType(1));
    ++value_;
    return *this;
  }
  const StrongInt operator++(int postfix_flag) {  // x++
    ValidatorType::template ValidateAdd<ValueType>(value_, ValueType(1));
    StrongInt temp(*this);
    ++value_;
    return temp;
  }
  StrongInt &operator--() {  // --x
    ValidatorType::template ValidateSubtract<ValueType>(value_, ValueType(1));
    --value_;
    return *this;
  }
  const StrongInt operator--(int postfix_flag) {  // x--
    ValidatorType::template ValidateSubtract<ValueType>(value_, ValueType(1));
    StrongInt temp(*this);
    --value_;
    return temp;
  }

  // Action-Assignment operators.
  StrongInt &operator+=(StrongInt arg) {
    ValidatorType::template ValidateAdd<ValueType>(value_, arg.value());
    value_ += arg.value();
    return *this;
  }
  StrongInt &operator-=(StrongInt arg) {
    ValidatorType::template ValidateSubtract<ValueType>(value_, arg.value());
    value_ -= arg.value();
    return *this;
  }
  template<typename ArgType>
  StrongInt &operator*=(ArgType arg) {
    ValidatorType::template ValidateMultiply<ValueType, ArgType>(value_, arg);
    value_ *= arg;
    return *this;
  }
  template<typename ArgType>
  StrongInt &operator/=(ArgType arg) {
    ValidatorType::template ValidateDivide<ValueType, ArgType>(value_, arg);
    value_ /= arg;
    return *this;
  }
  template<typename ArgType>
  StrongInt &operator%=(ArgType arg) {
    ValidatorType::template ValidateModulo<ValueType, ArgType>(value_, arg);
    value_ %= arg;
    return *this;
  }
  StrongInt &operator<<=(int64 arg) {            // NOLINT(whitespace/operators)
    ValidatorType::template ValidateLeftShift<ValueType>(value_, arg);
    value_ <<= arg;
    return *this;
  }
  StrongInt &operator>>=(int64 arg) {            // NOLINT(whitespace/operators)
    ValidatorType::template ValidateRightShift<ValueType>(value_, arg);
    value_ >>= arg;
    return *this;
  }
  StrongInt &operator&=(StrongInt arg) {
    ValidatorType::template ValidateBitAnd<ValueType>(value_, arg.value());
    value_ &= arg.value();
    return *this;
  }
  StrongInt &operator|=(StrongInt arg) {
    ValidatorType::template ValidateBitOr<ValueType>(value_, arg.value());
    value_ |= arg.value();
    return *this;
  }
  StrongInt &operator^=(StrongInt arg) {
    ValidatorType::template ValidateBitXor<ValueType>(value_, arg.value());
    value_ ^= arg.value();
    return *this;
  }

 private:
  // The integer value of type ValueType.
  ValueType value_;

  COMPILE_ASSERT(base::is_integral<ValueType>::value,
                 invalid_integer_type_for_strong_int_);
};

// Provide the << operator, primarily for logging purposes.
template<typename TagType, typename ValueType, typename ValidatorType>
std::ostream &operator<<(std::ostream &os,
                         StrongInt<TagType, ValueType, ValidatorType> arg) {
  return os << arg.value();
}

// Define operators that take two StrongInt arguments. These operators are
// defined in terms of their op-equal member function cousins.
#define STRONG_INT_VS_STRONG_INT_BINARY_OP(op)                                 \
  template<typename TagType, typename ValueType, typename ValidatorType>       \
  inline StrongInt<TagType, ValueType, ValidatorType>                          \
  operator op(StrongInt<TagType, ValueType, ValidatorType> lhs,                \
              StrongInt<TagType, ValueType, ValidatorType> rhs) {              \
    lhs op ## = rhs;                                                           \
    return lhs;                                                                \
  }
STRONG_INT_VS_STRONG_INT_BINARY_OP(+);
STRONG_INT_VS_STRONG_INT_BINARY_OP(-);
STRONG_INT_VS_STRONG_INT_BINARY_OP(&);
STRONG_INT_VS_STRONG_INT_BINARY_OP(|);
STRONG_INT_VS_STRONG_INT_BINARY_OP(^);
#undef STRONG_INT_VS_STRONG_INT_BINARY_OP

// Define operators that take one StrongInt and one native integer argument.
// These operators are defined in terms of their op-equal member function
// cousins, mostly.
#define STRONG_INT_VS_NUMERIC_BINARY_OP(op)                                    \
  template<typename TagType, typename ValueType, typename ValidatorType,       \
           typename NumType>                                                   \
  inline StrongInt<TagType, ValueType, ValidatorType>                          \
  operator op(StrongInt<TagType, ValueType, ValidatorType> lhs, NumType rhs) { \
    lhs op ## = rhs;                                                           \
    return lhs;                                                                \
  }
// This is used for commutative operators between one StrongInt and one native
// integer argument.  That is a long way of saying "multiplication".
#define NUMERIC_VS_STRONG_INT_BINARY_OP(op)                                    \
  template<typename TagType, typename ValueType, typename ValidatorType,       \
           typename NumType>                                                   \
  inline StrongInt<TagType, ValueType, ValidatorType>                          \
  operator op(NumType lhs, StrongInt<TagType, ValueType, ValidatorType> rhs) { \
    rhs op ## = lhs;                                                           \
    return rhs;                                                                \
  }
STRONG_INT_VS_NUMERIC_BINARY_OP(*);
NUMERIC_VS_STRONG_INT_BINARY_OP(*);
STRONG_INT_VS_NUMERIC_BINARY_OP(/);
STRONG_INT_VS_NUMERIC_BINARY_OP(%);
STRONG_INT_VS_NUMERIC_BINARY_OP(<<);             // NOLINT(whitespace/operators)
STRONG_INT_VS_NUMERIC_BINARY_OP(>>);             // NOLINT(whitespace/operators)
#undef STRONG_INT_VS_NUMERIC_BINARY_OP
#undef NUMERIC_VS_STRONG_INT_BINARY_OP

// Define comparison operators.  We allow all comparison operators.
#define STRONG_INT_COMPARISON_OP(op)                                           \
  template<typename TagType, typename ValueType, typename ValidatorType>       \
  inline bool operator op(StrongInt<TagType, ValueType, ValidatorType> lhs,    \
                          StrongInt<TagType, ValueType, ValidatorType> rhs) {  \
    return lhs.value() op rhs.value();                                         \
  }
STRONG_INT_COMPARISON_OP(==);                    // NOLINT(whitespace/operators)
STRONG_INT_COMPARISON_OP(!=);                    // NOLINT(whitespace/operators)
STRONG_INT_COMPARISON_OP(<);                     // NOLINT(whitespace/operators)
STRONG_INT_COMPARISON_OP(<=);                    // NOLINT(whitespace/operators)
STRONG_INT_COMPARISON_OP(>);                     // NOLINT(whitespace/operators)
STRONG_INT_COMPARISON_OP(>=);                    // NOLINT(whitespace/operators)
#undef STRONG_INT_COMPARISON_OP

}  // namespace util_intops

// Defines the StrongInt using value_type and typedefs it to type_name, with no
// validation of under/overflow situations.
// The struct int_type_name ## _tag_ trickery is needed to ensure that a new
// type is created per type_name.
#define DEFINE_STRONG_INT_TYPE(type_name, value_type)                          \
  struct type_name ## _strong_int_tag_ {};                                     \
  typedef ::util_intops::StrongInt<                                            \
      type_name ## _strong_int_tag_,                                           \
      value_type,                                                              \
      ::util_intops::NullStrongIntValidator> type_name;

// Allow StrongInt to be used as a key to hashable containers.
HASH_NAMESPACE_DECLARATION_START
template<typename Tag, typename Value, typename Validator>
struct hash<util_intops::StrongInt<Tag, Value, Validator> > {
  size_t operator()(
      const util_intops::StrongInt<Tag, Value, Validator> &idx) const {
    return static_cast<size_t>(idx.value());
  }
};
HASH_NAMESPACE_DECLARATION_END

#endif  // UTIL_INTOPS_STRONG_INT_H_
