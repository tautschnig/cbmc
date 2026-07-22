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
#include <util/mathematical_expr.h>
#include <util/mathematical_types.h>
#include <util/pointer_expr.h>
#include <util/std_expr.h>
#include <util/std_types.h>
#include <util/symbol_table_base.h>

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
  /// Complex number stored via __class_ptr pointing at a
  /// python_complex struct (real, imag : double). Separated
  /// from CLASS so python_truthiness and unwrap_value can
  /// dereference as a python_complex struct (which has
  /// .real / .imag at known offsets) and apply PLR §6.10.1
  /// truth-value semantics (0+0j is falsy).
  COMPLEX = 8,
  /// Set container stored via __class_ptr pointing at a python_set
  /// struct (bitmap, offset). Separated from CLASS / DICT so len(),
  /// truthiness, unwrap_value and the Any-receiver method dispatch can
  /// dereference it as a set struct.
  SET = 9,
  /// Fat-closure (PLR §4.2.2). __int_val holds the fn identity (an
  /// index into the converter's closure registry); __class_ptr points
  /// at a per-instance heap capture record (a struct with one field per
  /// captured free variable, or a cell pointer for nonlocal-mutated
  /// captures). Lets a closure carry its captures through any value
  /// channel (parameter, container element, attribute) so a call binds
  /// captures from the value it actually received — see
  /// doc/python-frontend-fat-closure-plan.md.
  CLOSURE = 10,
  /// Tuple stored via __class_ptr pointing at a python_tuple struct. Separated
  /// from CLASS so isinstance(x, tuple), truthiness, and structural comparison
  /// can treat a boxed tuple as a tuple rather than a user-class instance. See
  /// doc/python-frontend-tuple-tag-plan.md.
  TUPLE = 11,
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

/// Fixed-width pointer type used to box a heap smt_string on the native
/// SMT-String back-end (see python_value_struct_def's __str member).
inline pointer_typet python_boxed_string_ptr_type()
{
  return pointer_typet{python_string_type(), 64};
}

/// Fixed-width pointer type used to box a heap mathematical integer under
/// --python-unbounded-ints (see python_value_struct_def's __int_val member).
inline pointer_typet python_boxed_int_ptr_type()
{
  return pointer_typet{integer_typet{}, 64};
}

/// Type of python_value's __int_val member: a typed pointer to a heap
/// mathematical integer under --python-unbounded-ints ("int boxing"), or the
/// inline int64 otherwise. Boxing keeps python_value fixed-width (so it stays
/// byte_extract-valid) while preserving full integer precision -- an inline
/// integer_typet would both make python_value variable-width AND, today,
/// silently truncate to 64 bits when a value is wrapped into the tagged union.
inline typet python_value_int_member_type()
{
  // Unbounded: an INT-ID HANDLE (see python_int_handle_type) -- the
  // integer* box byte-extracted the pointed mathematical integer when
  // value sets failed to resolve (same family as string pointer boxing).
  if(python_unbounded_ints_flag())
    return python_int_handle_type();
  return signedbv_typet{64};
}

/// Type of python_value's __str member: a typed pointer to a heap smt_string
/// on the native SMT-String back-end ("string boxing"), or the inline string
/// struct on the refined back-end.
inline typet python_value_str_member_type()
{
  // Native backend: the __str payload is a STRING-ID HANDLE. The earlier
  // attempt was reverted because the smt2 regex lowering recovers string
  // CONSTANTS syntactically and a UF blocked it; the backend now has
  // strtab-aware recovery (smt2_convt::try_extract_string_literal follows
  // strtab(<const id>) through the front-end's intern symbols), so the
  // contract holds and python_value becomes fully fixed-width with no
  // byte-reachable pointer (the last native TOERR family).
  if(python_smt_string_native_flag())
    return python_string_handle_type();
  return python_string_type();
}

/// Return the actual struct definition for the Python tagged-union.
/// Only used to register the type in the symbol table.
inline struct_typet python_value_struct_def()
{
  struct_typet::componentst components;

  components.push_back(struct_typet::componentt{"__tag", signedbv_typet{32}});
  components.push_back(
    struct_typet::componentt{"__int_val", python_value_int_member_type()});
  components.push_back(struct_typet::componentt{"__float_val", double_type()});
  components.push_back(
    struct_typet::componentt{"__bool_val", signedbv_typet{32}});
  // Inline refined string OR, on the native SMT-String back-end, a typed
  // pointer to a heap smt_string ("string boxing"). Rationale: on native,
  // python_string_type() is the variable-width `smt_string` sort; storing it
  // inline makes python_value itself variable-width, so any byte-imaged
  // aggregate of python_value (e.g. an untyped dict's value array) hits
  // CBMC's unpack_struct "non-constant-width member must come last" invariant
  // and aborts. Boxing the string behind a fixed-width typed pointer keeps the
  // byte-imaged skeleton all-fixed-width (byte_extract stays valid), while the
  // actual smt_string is only ever touched through a clean typed dereference,
  // never byte-imaged. On the refined back-end the string is a fixed-width
  // struct, so it stays inline (the historical perf choice is preserved).
  components.push_back(
    struct_typet::componentt{"__str", python_value_str_member_type()});
  // List values use an opaque pointer (like __class_ptr) — typed
  // pointer would create a recursive type definition
  // (python_value -> pointer to python_list[python_value]) which
  // CBMC's smt2_conv emits as separate one-by-one declare-datatypes
  // statements, leading to forward-reference errors under cvc5.
  // Callers cast to the concrete list pointer type at use site
  // (see python_value_list).
  components.push_back(
    struct_typet::componentt{"__list_ptr", pointer_typet{empty_typet{}, 64}});
  // Opaque class-instance pointer. Used when CLASS tag is set;
  // the concrete struct type is not carried here — callers must
  // cast back to the specific class type at use site.
  components.push_back(
    struct_typet::componentt{"__class_ptr", pointer_typet{empty_typet{}, 64}});

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
  exprt int_val = python_unbounded_ints_flag()
                    ? exprt{from_integer(0, python_int_handle_type())}
                    : exprt{from_integer(0, signedbv_typet{64})};
  exprt float_val =
    ieee_floatt{
      ieee_float_spect::double_precision(),
      ieee_floatt::rounding_modet::ROUND_TO_EVEN}
      .to_expr();
  exprt bool_val = from_integer(0, signedbv_typet{32});
  // Inline empty string (the default __str). Native SMT-String back-end
  // (Plan A): __str is a typed pointer to a heap smt_string ("string
  // boxing"), so the default is a null string* (no inline smt_string, which
  // would re-introduce a variable-width member into the byte-imaged skeleton).
  exprt str_val =
    python_smt_string_native_flag()
      ? exprt{from_integer(0, python_string_handle_type())}
      : exprt{struct_exprt{
          {from_integer(0, signedbv_typet{64}),
           null_pointer_exprt{pointer_typet{unsignedbv_typet{8}, 64}}},
          python_string_type()}};
  exprt list_ptr = null_pointer_exprt{pointer_typet{empty_typet{}, 64}};
  exprt class_ptr = null_pointer_exprt{pointer_typet{empty_typet{}, 64}};

  switch(tag)
  {
  case python_type_tagt::INT:
    // Unbounded ("int boxing"): the wrap site passes &heap_integer, stored
    // directly as a typed integer* (no deref — python_value stays fixed-width).
    // Unbounded: the wrap site passes the HANDLE (int_to_handle); store
    // it directly.
    if(python_unbounded_ints_flag())
      int_val = is_python_int_handle_type(value.type())
                  ? value
                  : typecast_exprt{value, python_int_handle_type()};
    else
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
    // Native ("string boxing"): wrap_value passes &heap_smt_string, which we
    // store directly as a typed string* (no deref — keeping python_value
    // fixed-width). Refined: deref a pointer arg / store the inline string.
    // Native: the wrap site passes the HANDLE (string_to_handle); store
    // it directly.
    if(python_smt_string_native_flag())
      str_val = is_python_string_handle_type(value.type())
                  ? value
                  : typecast_exprt{value, python_string_handle_type()};
    else
      str_val = value.type().id() == ID_pointer
                  ? exprt{dereference_exprt{value}}
                  : value;
    break;
  case python_type_tagt::LIST:
    list_ptr = value.type().id() == ID_pointer
                 ? typecast_exprt{value, pointer_typet{empty_typet{}, 64}}
                 : typecast_exprt{
                     address_of_exprt{value}, pointer_typet{empty_typet{}, 64}};
    break;
  case python_type_tagt::CLASS:
    class_ptr =
      value.type().id() == ID_pointer
        ? typecast_exprt{value, pointer_typet{empty_typet{}, 64}}
        : typecast_exprt{
            address_of_exprt{value}, pointer_typet{empty_typet{}, 64}};
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
  case python_type_tagt::COMPLEX:
    // Complex stored via class_ptr -> python_complex struct.
    // Truthiness and unwrap_value dispatch on COMPLEX to
    // dereference and read real / imag fields.
    class_ptr =
      value.type().id() == ID_pointer
        ? typecast_exprt{value, pointer_typet{empty_typet{}, 64}}
        : typecast_exprt{
            address_of_exprt{value}, pointer_typet{empty_typet{}, 64}};
    break;
  case python_type_tagt::NONE:
    break;
  case python_type_tagt::SET:
    // Sets use the class_ptr slot to store the python_set-struct address.
    // The set struct is element-type-agnostic (a bitmap), so unlike list /
    // dict it can be shared by direct address with no element promotion.
    class_ptr =
      value.type().id() == ID_pointer
        ? typecast_exprt{value, pointer_typet{empty_typet{}, 64}}
        : typecast_exprt{
            address_of_exprt{value}, pointer_typet{empty_typet{}, 64}};
    break;
  case python_type_tagt::TUPLE:
    // Tuples use the class_ptr slot to store the python_tuple-struct address,
    // tagged TUPLE (not CLASS) so isinstance / truthiness / comparison treat it
    // as a tuple rather than a user-class instance.
    class_ptr =
      value.type().id() == ID_pointer
        ? typecast_exprt{value, pointer_typet{empty_typet{}, 64}}
        : typecast_exprt{
            address_of_exprt{value}, pointer_typet{empty_typet{}, 64}};
    break;
  case python_type_tagt::CLOSURE:
    // Built via make_python_closure (needs both fn index and record
    // pointer); not constructible through the single-value path.
    break;
  }

  struct_exprt result{
    {tag_expr, int_val, float_val, bool_val, str_val, list_ptr, class_ptr},
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
inline exprt python_value_int(const exprt &value)
{
  // Unbounded: __int_val is an INT-ID HANDLE; the denotation is inttab(h)
  // (an uninterpreted bv64 -> Int function; the converter guarantees the
  // symbol-table entry via inttab_symbol()). Otherwise the inline int64.
  if(python_unbounded_ints_flag())
  {
    return function_application_exprt{
      symbol_exprt{
        "python::__cbmc_inttab",
        mathematical_function_typet{{signedbv_typet{64}}, integer_typet{}}},
      {member_exprt{value, "__int_val", python_int_handle_type()}}};
  }
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

/// Extract the (inline) string from a tagged-union value.
inline exprt python_value_str(const exprt &value)
{
  // Native: __str is a string-id HANDLE; the denotation is strtab(h).
  // Constant payloads stay recoverable by the smt2 intrinsic lowerings
  // via the intern symbols (try_extract_string_literal). Refined: __str
  // is the inline string struct.
  if(python_smt_string_native_flag())
  {
    return python_string_handle_denotation(
      member_exprt{value, "__str", python_string_handle_type()});
  }
  return member_exprt{value, "__str", python_string_type()};
}

/// Extract the list pointer from a tagged-union value.
/// Returns a dereference of the typed list pointer; the underlying
/// `__list_ptr` field is opaque (`pointer_typet{empty_typet}`) to
/// avoid a recursive type definition, so we typecast at the use site.
inline dereference_exprt python_value_list(const exprt &value)
{
  return dereference_exprt{typecast_exprt{
    member_exprt{value, "__list_ptr", pointer_typet{empty_typet{}, 64}},
    pointer_typet{python_list_type(python_value_type()), 64}}};
}

/// Extract the class-instance pointer from a tagged-union value.
/// Returns an untyped (empty_typet) pointer; callers cast to the
/// specific class struct type.
inline member_exprt python_value_class_ptr(const exprt &value)
{
  return member_exprt{value, "__class_ptr", pointer_typet{empty_typet{}, 64}};
}

/// Check if a tagged-union value has a specific tag.
inline equal_exprt python_value_is(const exprt &value, python_type_tagt tag)
{
  return equal_exprt{
    python_value_tag(value),
    from_integer(static_cast<int>(tag), signedbv_typet{32})};
}

/// Build a fat-closure tagged-union value: `fn_index` identifies the
/// closure in the converter's registry (stored in __int_val) and
/// `record_ptr` points at its per-instance heap capture record (stored
/// in the opaque __class_ptr slot, cast at the dispatch site).
inline struct_exprt
make_python_closure(const exprt &fn_index_stored, const exprt &record_ptr)
{
  struct_typet vtype = python_value_struct_def();
  exprt tag_expr = from_integer(
    static_cast<int>(python_type_tagt::CLOSURE), signedbv_typet{32});
  exprt fn_val = fn_index_stored;
  exprt float_val =
    ieee_floatt{
      ieee_float_spect::double_precision(),
      ieee_floatt::rounding_modet::ROUND_TO_EVEN}
      .to_expr();
  exprt bool_val = from_integer(0, signedbv_typet{32});
  exprt str_val =
    python_smt_string_native_flag()
      ? exprt{from_integer(0, python_string_handle_type())}
      : exprt{struct_exprt{
          {from_integer(0, signedbv_typet{64}),
           null_pointer_exprt{pointer_typet{unsignedbv_typet{8}, 64}}},
          python_string_type()}};
  exprt list_ptr = null_pointer_exprt{pointer_typet{empty_typet{}, 64}};
  exprt class_ptr =
    record_ptr.type().id() == ID_pointer
      ? typecast_exprt{record_ptr, pointer_typet{empty_typet{}, 64}}
      : typecast_exprt{
          address_of_exprt{record_ptr}, pointer_typet{empty_typet{}, 64}};
  struct_exprt result{
    {tag_expr, fn_val, float_val, bool_val, str_val, list_ptr, class_ptr},
    vtype};
  result.type() = python_value_type();
  return result;
}

/// Extract the fn-registry index from a fat-closure value.
inline exprt python_value_closure_fn(const exprt &value)
{
  // Unbounded: __int_val is an INT-ID HANDLE; the closure fn identity is
  // inttab(h) (see python_value_int).
  if(python_unbounded_ints_flag())
  {
    return function_application_exprt{
      symbol_exprt{
        "python::__cbmc_inttab",
        mathematical_function_typet{{signedbv_typet{64}}, integer_typet{}}},
      {member_exprt{value, "__int_val", python_int_handle_type()}}};
  }
  return member_exprt{value, "__int_val", signedbv_typet{64}};
}

/// Extract the opaque capture-record pointer from a fat-closure value.
inline member_exprt python_value_closure_rec(const exprt &value)
{
  return member_exprt{value, "__class_ptr", pointer_typet{empty_typet{}, 64}};
}

/// PLR §3.2: the integer sentinel value used to encode None when
/// stored in a typed numeric (signedbv/integer/natural) slot. Used
/// because there is no distinguished bit pattern for None inside
/// signedbv. Centralised here so we don't have the magic number
/// scattered across the converter. Long term this encoding will
/// be replaced by `python_none_value()` everywhere — see
/// doc/python-frontend-plans.md (deferred residuals).
inline mp_integer python_none_sentinel_int()
{
  return mp_integer{-4611686018427387904LL}; // -2^62
}

/// Build the canonical NONE-tagged tagged-union value.
/// PLR §3.2: None is the single value of type NoneType. In the
/// python_value tagged union, NONE has its own tag and no payload.
inline struct_exprt python_none_value()
{
  return make_python_value(
    python_type_tagt::NONE, from_integer(0, signedbv_typet{64}));
}

/// Check if `e` is a constant expression representing None in any
/// of our encodings:
///   * legacy: a signedbv constant equal to the None sentinel
///   * tagged: a python_value struct literal with NONE tag
/// This recognizer lets consumer sites accept both forms during the
/// gradual migration to the python_none_value() encoding.
inline bool is_python_none_constant(const exprt &e)
{
  // Legacy form: signedbv constant with sentinel value.
  if(e.is_constant() && e.type().id() == ID_signedbv)
  {
    mp_integer v;
    if(!to_integer(to_constant_expr(e), v) && v == python_none_sentinel_int())
      return true;
  }
  // Tagged form: python_value struct literal with NONE tag.
  if(
    e.id() == ID_struct && is_python_value_type(e.type()) &&
    e.operands().size() >= 1)
  {
    const exprt &tag_op = e.operands()[0];
    if(tag_op.is_constant() && tag_op.type().id() == ID_signedbv)
    {
      mp_integer t;
      if(
        !to_integer(to_constant_expr(tag_op), t) &&
        t == mp_integer{static_cast<int>(python_type_tagt::NONE)})
        return true;
    }
  }
  return false;
}

/// Broader None recognizer: in addition to the literal forms
/// matched by `is_python_none_constant`, also recognises a
/// symbol-expression whose stored value is python_value{NONE}.
///
/// Imported-module pre-pass freezes per-FunctionDef defaults at
/// module level into static `__def_<func>_<idx>` symbols (see
/// python_converter_module.cpp). For default-bound `None` the
/// symbol's stored value is a python_value{NONE} struct
/// literal, but the expression we see at the boundary is the
/// symbol_expr — `is_python_none_constant` would miss it.
///
/// Use this predicate at any site that needs to recognise None
/// regardless of whether it was inlined as a literal or bound
/// through the defaults loop. Use the narrower
/// `is_python_none_constant` at sites that explicitly want
/// only the literal form (e.g. inside `unwrap_value`'s early-
/// exit, where recursive symbol lookup could be expensive).
inline bool
is_python_none(const exprt &e, const symbol_table_baset &symbol_table)
{
  if(is_python_none_constant(e))
    return true;
  if(e.id() == ID_symbol)
  {
    const symbolt *s = symbol_table.lookup(to_symbol_expr(e).get_identifier());
    if(s != nullptr && is_python_none_constant(s->value))
      return true;
  }
  return false;
}

#endif // CPROVER_PYTHON_PYTHON_VALUE_TYPE_H
