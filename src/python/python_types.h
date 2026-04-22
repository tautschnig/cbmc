/// \file
/// Python type representations

#ifndef CPROVER_PYTHON_PYTHON_TYPES_H
#define CPROVER_PYTHON_PYTHON_TYPES_H

#include <util/bitvector_types.h>
#include <util/std_types.h>

/// Maximum length for Python strings in verification.
/// Can be overridden with --python-max-string-length.
#define PYTHON_MAX_STRING_LENGTH 256

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

#endif // CPROVER_PYTHON_PYTHON_TYPES_H
