/// \file
/// TypeScript type helpers for CBMC

#ifndef CPROVER_TYPESCRIPT_TYPESCRIPT_TYPES_H
#define CPROVER_TYPESCRIPT_TYPESCRIPT_TYPES_H

#include <util/arith_tools.h>
#include <util/bitvector_types.h>
#include <util/c_types.h>
#include <util/ieee_float.h>
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

/// ES2024 sec-ecmascript-language-types-string-type:
/// "The String type is the set of all ordered sequences of zero or more
///  16-bit unsigned integer values."
///
/// handled by CBMC's string solver (--refine-strings).
// ES2024 sec-ecmascript-language-types-string-type:
// Strings are sequences of UTF-16 code units (unsignedbv[16]).
// Modeled as struct{length: signedbv[32], data: unsignedbv[16][MAX]}.
#define TYPESCRIPT_MAX_STRING_LENGTH 64

inline struct_typet typescript_string_type()
{
  struct_typet::componentst components;
  components.push_back(struct_typet::componentt{"length", signedbv_typet{32}});
  components.push_back(struct_typet::componentt{
    "data",
    array_typet{
      unsignedbv_typet{16},
      from_integer(TYPESCRIPT_MAX_STRING_LENGTH, signedbv_typet{64})}});
  struct_typet result{components};
  result.set_tag("typescript_string");
  return result;
}

inline bool is_typescript_string_type(const typet &type)
{
  return type.id() == ID_struct &&
         to_struct_type(type).get_tag() == "typescript_string";
}

#endif // CPROVER_TYPESCRIPT_TYPESCRIPT_TYPES_H
