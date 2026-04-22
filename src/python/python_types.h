/// \file
/// Python type representations

#ifndef CPROVER_PYTHON_PYTHON_TYPES_H
#define CPROVER_PYTHON_PYTHON_TYPES_H

#include <util/arith_tools.h>
#include <util/bitvector_types.h>
#include <util/std_types.h>

/// Maximum length for Python strings in verification.
/// Can be overridden with --python-max-string-length.
#define PYTHON_MAX_STRING_LENGTH 256

/// Maximum length for Python lists in verification.
#define PYTHON_MAX_LIST_LENGTH 64

/// Return the CBMC type used to represent Python str.
/// This is a struct { signedbv[64] length; unsignedbv[8] data[N]; }
inline struct_typet python_string_type()
{
  struct_typet::componentst components;

  struct_typet::componentt length{"length", signedbv_typet{64}};
  components.push_back(length);

  struct_typet::componentt data{
    "data",
    array_typet{
      unsignedbv_typet{8},
      from_integer(PYTHON_MAX_STRING_LENGTH, signedbv_typet{64})}};
  components.push_back(data);

  struct_typet result{components};
  result.set_tag("python_str");
  return result;
}

/// Check if a type is a Python string type.
inline bool is_python_string_type(const typet &type)
{
  if(type.id() != ID_struct)
    return false;
  const auto &st = to_struct_type(type);
  return st.get_tag() == "python_str";
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
