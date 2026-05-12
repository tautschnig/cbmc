/// \file
/// TypeScript type helpers for CBMC

#ifndef CPROVER_TYPESCRIPT_TYPESCRIPT_TYPES_H
#define CPROVER_TYPESCRIPT_TYPESCRIPT_TYPES_H

#include <util/arith_tools.h>
#include <util/bitvector_types.h>
#include <util/c_types.h>
#include <util/cprover_prefix.h>
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

/// ES2024 sec-ecmascript-language-types-string-type:
/// "The String type is the set of all ordered sequences of zero or more
///  16-bit unsigned integer values."
///
/// Strings are tagged with CBMC's refined-string-type tag so the
/// refined string solver (`--refine-strings`) can recognise them.
/// The struct shape stays compatible with our existing fixed-size
/// model:
///   - length:  signedbv[32]
///   - data:    unsignedbv[16] array (inline, max TYPESCRIPT_MAX_STRING_LENGTH)
/// Auto-enabled for .ts/.tsx files in cbmc_parse_options.cpp.
// ES2024 sec-ecmascript-language-types-string-type:
// Strings are sequences of UTF-16 code units (unsignedbv[16]).
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
  // Use a TypeScript-specific tag. DO NOT use CPROVER_PREFIX
  // "refined_string_type" — that tag is reserved for the string
  // solver's own refined_string_typet, which has a completely
  // different structure ({length: index_type, content: char*}).
  // Sharing the tag caused is_refined_string_type() to return true
  // for our inline-array struct, confusing the solver.
  result.set_tag("typescript_string");
  return result;
}

inline bool is_typescript_string_type(const typet &type)
{
  return type.id() == ID_struct &&
         to_struct_type(type).get_tag() == "typescript_string";
}

#endif // CPROVER_TYPESCRIPT_TYPESCRIPT_TYPES_H
