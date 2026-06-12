/// \file
/// Python type representations for CBMC verification.
///
/// Models Python's built-in types as CBMC struct/array types:
///
/// - int:  signedbv[64] (PLR §3.2: "Integers have unlimited precision" —
///         we approximate with 64-bit; use --python-unbounded-ints for exact)
/// - float: IEEE 754 double (PLR §3.2: "double-precision floating-point")
/// - bool: bool_typet (PLR §3.2: "Booleans are a subtype of integers")
/// - str:  struct { int64 length; uint8 data[256]; }
///         (PLR §3.2: "immutable sequences of Unicode code points" —
///         bounded approximation; content tracked for literals and concat)
/// - list: struct { int64 length; T data[64]; }
///         (PLR §3.2: "mutable sequences" — bounded approximation)
/// - tuple: struct { T _0; T _1; ... }
///         (PLR §3.2: "immutable sequences" — fixed-size struct)
/// - dict: struct { int64 length; K keys[16]; V values[16]; }
///         (PLR §3.2: "mutable mappings" — bounded approximation)

#ifndef CPROVER_PYTHON_PYTHON_TYPES_H
#define CPROVER_PYTHON_PYTHON_TYPES_H

#include <util/arith_tools.h>
#include <util/bitvector_types.h>
#include <util/cprover_prefix.h>
#include <util/mathematical_expr.h>
#include <util/pointer_expr.h>
#include <util/refined_string_type.h>
#include <util/std_types.h>
#include <util/string_expr.h>

/// Maximum length for Python strings in verification.
/// Can be overridden with --python-max-string-length.
#define PYTHON_STRING_TAG "tag-__CPROVER_refined_string_type"

#ifndef PYTHON_MAX_STRING_LENGTH
#  define PYTHON_MAX_STRING_LENGTH 64
#endif

/// Maximum length for Python lists in verification.
/// Can be overridden with --python-max-list-length.
#ifndef PYTHON_MAX_LIST_LENGTH
#  define PYTHON_MAX_LIST_LENGTH 64
#endif

/// Process-wide flag: when true (set by the Python converter under
/// --python-smt-strings-native), Python `str` is represented as the native
/// SMT String sort rather than the refined-string struct.
inline bool &python_smt_string_native_flag()
{
  static bool flag = false;
  return flag;
}

/// Return the CBMC type used to represent Python str.
/// Default: a struct { signedbv[64] length; unsignedbv[8] data[N]; }.
/// Native SMT-String backend: the SMT `String` sort (smt_string_typet).
inline typet python_string_type()
{
  if(python_smt_string_native_flag())
    return smt_string_typet{};
  return struct_tag_typet{PYTHON_STRING_TAG};
}

inline struct_typet python_string_struct_def()
{
  struct_typet::componentst components;
  components.push_back(struct_typet::componentt{"length", signedbv_typet{64}});
  components.push_back(struct_typet::componentt{
    "data", pointer_typet(unsignedbv_typet{8}, 64)});
  struct_typet result(components);
  result.set_tag(CPROVER_PREFIX "refined_string_type");
  return result;
}

/// Check if a type is a Python string type.
inline bool is_python_string_type(const typet &type)
{
  // Native SMT-String back-end (Plan A): a string value is the SMT String
  // sort rather than the refined struct.
  if(type.id() == ID_smt_string)
    return true;
  if(type.id() == ID_struct_tag)
    return to_struct_tag_type(type).get_identifier() == PYTHON_STRING_TAG;
  return is_refined_string_type(type);
}

/// Build a CBMC struct type for a Python tuple with the given element types.
inline struct_typet python_tuple_type(const std::vector<typet> &element_types)
{
  struct_typet::componentst components;
  for(std::size_t i = 0; i < element_types.size(); i++)
  {
    struct_typet::componentt comp{"_" + std::to_string(i), element_types[i]};
    components.push_back(comp);
  }
  struct_typet result{components};
  result.set_tag("python_tuple");
  return result;
}

/// Check if a type is a Python tuple type.
inline bool is_python_tuple_type(const typet &type)
{
  if(type.id() != ID_struct)
    return false;
  const auto &st = to_struct_type(type);
  return st.get_tag() == "python_tuple";
}

/// Check if a type is a Python dict type.
inline bool is_python_dict_type(const typet &type)
{
  if(type.id() != ID_struct)
    return false;
  const auto &st = to_struct_type(type);
  return id2string(st.get_tag()).substr(0, 11) == "python_dict";
}

/// Tag name for the python_set type in the symbol table.
#define PYTHON_SET_TAG "tag-python_set"

/// Return the canonical type reference for Python set values (bitmap
/// representation). Returns a struct_tag_typet that refers to the
/// actual struct definition in the symbol table (registered by
/// python_convertert::convert()). Using struct_tag_typet ensures
/// CBMC sees a single named type rather than fresh struct_typet
/// instances at every call site.
inline struct_tag_typet python_set_type()
{
  return struct_tag_typet{PYTHON_SET_TAG};
}

/// Actual struct definition for Python set: bitmap representation.
/// struct { uint64 bitmap; int64 offset; }
/// Bit i set ↔ element (offset + i) is in the set.
inline struct_typet python_set_struct_def()
{
  struct_typet::componentst components;
  components.push_back(
    struct_typet::componentt{"bitmap", unsignedbv_typet{64}});
  components.push_back(struct_typet::componentt{"offset", signedbv_typet{64}});
  struct_typet result{components};
  result.set_tag("python_set");
  return result;
}

/// Check if a type is a Python set (bitmap).
inline bool is_python_set_type(const typet &type)
{
  if(type.id() == ID_struct_tag)
    return to_struct_tag_type(type).get_identifier() == PYTHON_SET_TAG;
  if(type.id() != ID_struct)
    return false;
  return to_struct_type(type).get_tag() == "python_set";
}

#define PYTHON_MAX_DICT_SIZE 16

/// Return the CBMC type for an array-based Python dict.
/// struct { int64 length; key_type keys[N]; value_type values[N]; }
inline struct_typet
python_dict_type(const typet &key_type, const typet &value_type)
{
  struct_typet::componentst components;
  components.push_back(struct_typet::componentt{"length", signedbv_typet{64}});
  components.push_back(struct_typet::componentt{
    "keys",
    array_typet{
      key_type, from_integer(PYTHON_MAX_DICT_SIZE, signedbv_typet{64})}});
  components.push_back(struct_typet::componentt{
    "values",
    array_typet{
      value_type, from_integer(PYTHON_MAX_DICT_SIZE, signedbv_typet{64})}});
  struct_typet result{components};
  result.set_tag("python_dict_array");
  return result;
}

/// Return the CBMC type used to represent Python list[T].
/// This is a struct { int64 length; T data[MAX_LIST_LENGTH]; }
inline struct_typet python_list_type(const typet &element_type)
{
  struct_typet::componentst components;

  struct_typet::componentt length{"length", signedbv_typet{64}};
  components.push_back(length);

  struct_typet::componentt data{
    "data",
    array_typet{
      element_type, from_integer(PYTHON_MAX_LIST_LENGTH, signedbv_typet{64})}};
  components.push_back(data);

  struct_typet result{components};
  result.set_tag("python_list");
  return result;
}

/// Check if a type is a Python list type.
inline bool is_python_list_type(const typet &type)
{
  if(type.id() != ID_struct)
    return false;
  const auto &st = to_struct_type(type);
  return st.get_tag() == "python_list";
}

#endif // CPROVER_PYTHON_PYTHON_TYPES_H
