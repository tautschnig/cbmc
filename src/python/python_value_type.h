/// \file
/// Python tagged-union value type for dynamic typing.
///
/// PLR §3.1: "Every object has an identity, a type and a value."
/// PLR §3.2: "The standard type hierarchy"
///
/// Python variables are dynamically typed — a name can refer to objects
/// of different types at different times. When a function parameter has
/// no type annotation, we model it as a tagged union that can hold any
/// of the supported types. This is the python_value_type struct:
///
///   struct { int32 tag; int64 int_val; double float_val; bool bool_val;
///            str* str_ptr; list* list_ptr; }
///
/// The tag field identifies which field is active (see python_type_tagt).
/// String and list values use pointers to avoid bloating the union.

#ifndef CPROVER_PYTHON_PYTHON_VALUE_TYPE_H
#define CPROVER_PYTHON_PYTHON_VALUE_TYPE_H

#include <util/arith_tools.h>
#include <util/bitvector_types.h>
#include <util/c_types.h>
#include <util/ieee_float.h>
#include <util/pointer_expr.h>
#include <util/std_expr.h>
#include <util/std_types.h>

#include "python_types.h"

/// Type tags for the Python tagged-union value type.
enum class python_type_tagt
{
  NONE = 0,
  INT = 1,
  FLOAT = 2,
  BOOL = 3,
  STR = 4,
  LIST = 5,
  /// Class instance — the struct pointed to by __class_ptr.
  /// The user-defined class type is not tracked here; callers
  /// that need per-class dispatch should look up
  /// ``__class_tag`` on the pointed-to struct (maintained by
  /// the Python frontend's class-hierarchy machinery).
  CLASS = 6,
  /// Dict container stored via __class_ptr. Separated from
  /// CLASS so len() and similar operations can reliably
  /// dereference as a dict struct (which has .length at
  /// offset 0) without risking confusion with user-class
  /// instances whose first field is __class_tag.
  DICT = 7,
};

/// Tag name for the python_value type in the symbol table.
#define PYTHON_VALUE_TAG "tag-python_value"

/// Return the canonical type reference for Python tagged-union values.
/// This is a struct_tag_typet that refers to the actual struct definition
/// in the symbol table (registered by python_convertert::convert()).
/// Using struct_tag_typet enables self-referential types (list[python_value]).
inline struct_tag_typet python_value_type()
{
  return struct_tag_typet{PYTHON_VALUE_TAG};
}

/// Return the actual struct definition for the Python tagged-union.
/// Only used to register the type in the symbol table.
inline struct_typet python_value_struct_def()
{
  struct_typet::componentst components;

  components.push_back(struct_typet::componentt{"__tag", signedbv_typet{32}});
  components.push_back(
    struct_typet::componentt{"__int_val", signedbv_typet{64}});
  components.push_back(struct_typet::componentt{"__float_val", double_type()});
  components.push_back(
    struct_typet::componentt{"__bool_val", signedbv_typet{32}});
  components.push_back(struct_typet::componentt{
    "__str_ptr", pointer_typet{python_string_type(), 64}});
  // list[python_value_type] — self-referential via struct_tag_typet
  components.push_back(struct_typet::componentt{
    "__list_ptr", pointer_typet{python_list_type(python_value_type()), 64}});
  // Opaque class-instance pointer. Used when CLASS tag is set;
  // the concrete struct type is not carried here — callers must
  // cast back to the specific class type at use site.
  components.push_back(struct_typet::componentt{
    "__class_ptr", pointer_typet{empty_typet{}, 64}});

  struct_typet result{components};
  result.set_tag("python_value");
  return result;
}

/// Check if a type is the Python tagged-union value type.
inline bool is_python_value_type(const typet &type)
{
  if(type.id() == ID_struct_tag)
    return to_struct_tag_type(type).get_identifier() == PYTHON_VALUE_TAG;
  if(type.id() == ID_struct)
    return to_struct_type(type).get_tag() == "python_value";
  return false;
}

/// Build a tagged-union value expression from a concrete typed value.
inline struct_exprt make_python_value(python_type_tagt tag, const exprt &value)
{
  struct_typet vtype = python_value_struct_def();

  exprt tag_expr = from_integer(static_cast<int>(tag), signedbv_typet{32});
  exprt int_val = from_integer(0, signedbv_typet{64});
  exprt float_val =
    ieee_floatt{
      ieee_float_spect::double_precision(),
      ieee_floatt::rounding_modet::ROUND_TO_EVEN}
      .to_expr();
  exprt bool_val = from_integer(0, signedbv_typet{32});
  exprt str_ptr = null_pointer_exprt{
    pointer_typet{python_string_type(), 64}};
  exprt list_ptr = null_pointer_exprt{
    pointer_typet{python_list_type(python_value_type()), 64}};
  exprt class_ptr = null_pointer_exprt{
    pointer_typet{empty_typet{}, 64}};

  switch(tag)
  {
  case python_type_tagt::INT:
    int_val = value.type().id() == ID_signedbv
                ? value
                : typecast_exprt{value, signedbv_typet{64}};
    break;
  case python_type_tagt::FLOAT:
    float_val = value;
    break;
  case python_type_tagt::BOOL:
    bool_val = value.type().id() == ID_bool
                 ? typecast_exprt{value, signedbv_typet{32}}
                 : value;
    break;
  case python_type_tagt::STR:
    str_ptr = value.type().id() == ID_pointer
                ? value
                : address_of_exprt{value};
    break;
  case python_type_tagt::LIST:
    list_ptr = value.type().id() == ID_pointer
                 ? value
                 : address_of_exprt{value};
    break;
  case python_type_tagt::CLASS:
    class_ptr = value.type().id() == ID_pointer
                  ? typecast_exprt{value, pointer_typet{empty_typet{}, 64}}
                  : typecast_exprt{
                      address_of_exprt{value},
                      pointer_typet{empty_typet{}, 64}};
    break;
  case python_type_tagt::DICT:
    // Dicts use the class_ptr slot to store the dict-struct
    // address. len() and unwrap_value read back via DICT tag
    // to distinguish from user class instances.
    class_ptr =
      value.type().id() == ID_pointer
        ? typecast_exprt{value, pointer_typet{empty_typet{}, 64}}
        : typecast_exprt{
            address_of_exprt{value}, pointer_typet{empty_typet{}, 64}};
    break;
  case python_type_tagt::NONE:
    break;
  }

  struct_exprt result{
    {tag_expr, int_val, float_val, bool_val, str_ptr, list_ptr, class_ptr},
    vtype};
  // Set the expression type to the canonical tag type
  result.type() = python_value_type();
  return result;
}

/// Extract the tag from a tagged-union value.
inline member_exprt python_value_tag(const exprt &value)
{
  return member_exprt{value, "__tag", signedbv_typet{32}};
}

/// Extract the int field from a tagged-union value.
inline member_exprt python_value_int(const exprt &value)
{
  return member_exprt{value, "__int_val", signedbv_typet{64}};
}

/// Extract the float field from a tagged-union value.
inline member_exprt python_value_float(const exprt &value)
{
  return member_exprt{value, "__float_val", double_type()};
}

/// Extract the bool field from a tagged-union value.
inline member_exprt python_value_bool(const exprt &value)
{
  return member_exprt{value, "__bool_val", signedbv_typet{32}};
}

/// Extract the string pointer from a tagged-union value.
inline dereference_exprt python_value_str(const exprt &value)
{
  return dereference_exprt{
    member_exprt{value, "__str_ptr", pointer_typet{python_string_type(), 64}}};
}

/// Extract the list pointer from a tagged-union value.
inline dereference_exprt python_value_list(const exprt &value)
{
  return dereference_exprt{member_exprt{
    value,
    "__list_ptr",
    pointer_typet{python_list_type(python_value_type()), 64}}};
}

/// Extract the class-instance pointer from a tagged-union value.
/// Returns an untyped (empty_typet) pointer; callers cast to the
/// specific class struct type.
inline member_exprt python_value_class_ptr(const exprt &value)
{
  return member_exprt{
    value, "__class_ptr", pointer_typet{empty_typet{}, 64}};
}

/// Check if a tagged-union value has a specific tag.
inline equal_exprt python_value_is(const exprt &value, python_type_tagt tag)
{
  return equal_exprt{
    python_value_tag(value),
    from_integer(static_cast<int>(tag), signedbv_typet{32})};
}

#endif // CPROVER_PYTHON_PYTHON_VALUE_TYPE_H
