/// \file
/// TypeScript type helpers for CBMC

#ifndef CPROVER_TYPESCRIPT_TYPESCRIPT_TYPES_H
#define CPROVER_TYPESCRIPT_TYPESCRIPT_TYPES_H

#include <util/arith_tools.h>
#include <util/bitvector_types.h>
#include <util/ieee_float.h>
#include <util/std_types.h>
#include <util/c_types.h>

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

/// Maximum string length for TypeScript strings.
/// TODO: Replace with refined_string_typet for variable-length strings.
#define TYPESCRIPT_MAX_STRING_LENGTH 64

/// TypeScript `string` type — fixed-size array model.
/// ES2024 sec-ecmascript-language-types-string-type:
/// "The String type is the set of all ordered sequences of zero or more
///  16-bit unsigned integer values."
/// TODO: Migrate to refined_string_typet with --refine-strings
inline struct_typet typescript_string_type()
{
  struct_typet::componentst components;
  components.push_back(
    struct_typet::componentt{"length", signedbv_typet{64}});
  components.push_back(struct_typet::componentt{
    "data",
    array_typet{
      unsignedbv_typet{16},
      from_integer(TYPESCRIPT_MAX_STRING_LENGTH, signedbv_typet{64})}});
  struct_typet result{components};
  result.set_tag("typescript_str");
  return result;
}

inline bool is_typescript_string_type(const typet &type)
{
  if(type.id() != ID_struct)
    return false;
  return to_struct_type(type).get_tag() == "typescript_str";
}

#endif // CPROVER_TYPESCRIPT_TYPESCRIPT_TYPES_H
