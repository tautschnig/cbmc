/// \file
/// TypeScript type helpers for CBMC

#ifndef CPROVER_TYPESCRIPT_TYPESCRIPT_TYPES_H
#define CPROVER_TYPESCRIPT_TYPESCRIPT_TYPES_H

#include <util/arith_tools.h>
#include <util/bitvector_types.h>
#include <util/c_types.h>
#include <util/ieee_float.h>
#include <util/refined_string_type.h>
#include <util/std_types.h>

/// TypeScript `number` type — IEEE 754 double-precision float.
/// ES2024 sec-ecmascript-language-types-number-type
inline typet typescript_number_type()
{
  return double_type();
}

/// TypeScript `boolean` type.
/// ES2024 sec-ecmascript-language-types-boolean-type
inline typet typescript_boolean_type()
{
  return bool_typet{};
}

/// TypeScript `string` type — uses CBMC's refined_string_typet.
/// ES2024 sec-ecmascript-language-types-string-type:
/// "The String type is the set of all ordered sequences of zero or more
///  16-bit unsigned integer values."
///
/// refined_string_typet is a struct { length: index_type, content: pointer }
/// handled by CBMC's string solver (--refine-strings).
inline refined_string_typet typescript_string_type()
{
  return refined_string_typet{
    signedbv_typet{32},
    pointer_typet{unsignedbv_typet{16}, 64}};
}

inline bool is_typescript_string_type(const typet &type)
{
  return is_refined_string_type(type);
}

#endif // CPROVER_TYPESCRIPT_TYPESCRIPT_TYPES_H
