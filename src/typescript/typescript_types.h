/// \file
/// TypeScript type helpers for CBMC

#ifndef CPROVER_TYPESCRIPT_TYPESCRIPT_TYPES_H
#define CPROVER_TYPESCRIPT_TYPESCRIPT_TYPES_H

#include <util/bitvector_types.h>
#include <util/ieee_float.h>
#include <util/std_types.h>

/// TypeScript `number` type — IEEE 754 double-precision float.
/// ES2024 §6.1.6.1: "The Number type has exactly 18,437,736,874,454,810,627
/// values, representing the double-precision 64-bit format IEEE 754-2019
/// values."
inline typet typescript_number_type()
{
  return double_type();
}

/// TypeScript `boolean` type.
/// ES2024 §6.1.1: "The Boolean type represents a logical entity having
/// two values, called true and false."
inline typet typescript_boolean_type()
{
  return bool_typet{};
}

#endif // CPROVER_TYPESCRIPT_TYPESCRIPT_TYPES_H
