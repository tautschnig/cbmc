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
#include <util/mathematical_types.h>
#include <util/pointer_expr.h>
#include <util/refined_string_type.h>
#include <util/std_types.h>
#include <util/string_expr.h>

#include <map>
#include <string>

/// Maximum length for Python strings in verification.
/// Can be overridden with --python-max-string-length.
#define PYTHON_STRING_TAG "tag-__CPROVER_refined_string_type"

#ifndef PYTHON_MAX_STRING_LENGTH
#  define PYTHON_MAX_STRING_LENGTH 64
#endif

/// Maximum length for Python lists in verification.
/// Can be overridden with --python-max-list-length.
#ifndef PYTHON_MAX_LIST_LENGTH
#  define PYTHON_MAX_LIST_LENGTH 16
#endif

/// Process-wide flag: when true (set by the Python converter under
/// --python-smt-strings), Python `str` is represented as the native
/// SMT String sort rather than the refined-string struct.
inline bool &python_smt_string_native_flag()
{
  static bool flag = false;
  return flag;
}

/// Process-wide flag: when true (set by the Python converter under
/// --python-unbounded-ints), Python `int` is represented as the mathematical
/// (arbitrary-precision) `integer_typet` rather than int64. Because that type
/// is non-fixed-width, an int stored inside a byte-imaged aggregate (e.g.
/// python_value.__int_val) must be boxed behind a typed pointer, exactly like
/// the native smt_string case (see python_value_struct_def).
inline bool &python_unbounded_ints_flag()
{
  static bool flag = false;
  return flag;
}

/// Return the CBMC type used to represent Python str.
/// Default: a struct { signedbv[64] length; unsignedbv[8] data[N]; }.
/// Native SMT-String backend: the SMT `String` sort (string_typet).
inline typet python_string_type()
{
  if(python_smt_string_native_flag())
    return string_typet{};
  return struct_tag_typet{PYTHON_STRING_TAG};
}

/// Native SMT-String backend, STRING-ID HANDLE (strings plan, 2026-07-20):
/// the fixed-width in-aggregate representation of a str value. A handle is
/// a plain signedbv[64] whose STRING denotation is `(strtab h)` -- an
/// uninterpreted function bv64 -> String declared solver-side. Aggregates
/// stay fixed-width (byte-imaging an int is always well-defined, and the
/// solver-side association survives struct copies), which is what pointer
/// boxing could NOT deliver (an unresolved deref byte-extracts the pointed
/// variable-width string). The ID_C_ flag is an irept COMMENT: the handle
/// type compares EQUAL to signedbv[64] everywhere (no type-mismatch
/// ripples); the choke points (attribute read/write) read the flag off the
/// declared STRUCT COMPONENT type, which is stable.
inline typet python_string_handle_type()
{
  signedbv_typet t{64};
  t.set(ID_C_python_string_handle, true);
  return t;
}

inline bool is_python_string_handle_type(const typet &t)
{
  return t.id() == ID_signedbv && to_signedbv_type(t).get_width() == 64 &&
         t.get_bool(ID_C_python_string_handle);
}

/// Unbounded-ints INT-ID HANDLE (the strtab pattern for mathematical
/// integers): fixed-width in-aggregate/in-union representation denoting
/// `inttab(h)` -- an uninterpreted bv64 -> Int function. The integer* box
/// carried the pointer weakness (unresolved deref byte-extracts the
/// variable-width pointed integer -- probe-confirmed same unpack_struct
/// family). Unlike the pv __str payload (regex intrinsics recover string
/// CONSTANTS syntactically, blocking UFs), integer payloads feed
/// ARITHMETIC, which accepts any Int term.
inline typet python_int_handle_type()
{
  signedbv_typet t{64};
  t.set(ID_C_python_int_handle, true);
  return t;
}

inline bool is_python_int_handle_type(const typet &t)
{
  return t.id() == ID_signedbv && to_signedbv_type(t).get_width() == 64 &&
         t.get_bool(ID_C_python_int_handle);
}

/// The Int denotation of an int-id handle: inttab(h) (the uninterpreted
/// bv64 -> Int table; the converter guarantees the symbol-table entry
/// via inttab_symbol()). The read-side choke point for handle-typed int
/// slots (class fields under --python-unbounded-ints).
inline exprt python_int_handle_denotation(const exprt &handle)
{
  return function_application_exprt{
    symbol_exprt{
      "python::__cbmc_inttab",
      mathematical_function_typet{{signedbv_typet{64}}, integer_typet{}}},
    {handle}};
}

/// The String denotation of a string-id handle: strtab(h). A free-function
/// twin of python_convertert::string_handle_to_string for sites without
/// converter access (the converter seeds the strtab symbol-table entry at
/// conversion start whenever the native backend is active).
/// Reverse intern table (id -> constant text), mirrored from the
/// converter's string_to_handle interning. Global by the same convention
/// as the backend-kind flags: free functions (python_value_str, the
/// denotation below) need it without converter access.
inline std::map<long long, std::string> &python_string_intern_reverse()
{
  static std::map<long long, std::string> m;
  return m;
}

inline exprt python_string_handle_denotation(const exprt &handle)
{
  // CONSTANT-FOLD an interned handle's denotation to the string constant
  // itself: a strtab(<const>) UF application is NOT a constant, so symex
  // constant propagation cannot carry it into the regex/string intrinsic
  // operands (the smt2 lowering then loses compile-time precision). The
  // global ASSUME strtab(id) == "<const>" makes the fold sound (same
  // value); the smt2-side recovery (try_extract_string_literal) remains
  // for handle constants that only appear after simplification.
  if(handle.is_constant())
  {
    mp_integer id_val;
    if(!to_integer(to_constant_expr(handle), id_val))
    {
      auto &rev = python_string_intern_reverse();
      auto it = rev.find(id_val.to_long());
      if(it != rev.end())
        return constant_exprt{irep_idt{it->second}, string_typet{}};
    }
  }
  return function_application_exprt{
    symbol_exprt{
      "python::__cbmc_strtab",
      mathematical_function_typet{{signedbv_typet{64}}, string_typet{}}},
    {handle}};
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
  if(type.id() == ID_string)
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
/// On the native SMT-String back-end, a dict's string KEYS are boxed behind a
/// typed pointer (exactly like python_value.__str), because an inline
/// smt_string key array is variable-width and makes the byte-imaged dict struct
/// abort CBMC's unpack_struct ("non-constant-width member must come last").
/// The fixed-width pointer keeps the struct byte_extract-valid; the smt_string
/// is read through a clean typed dereference, never byte-imaged. Non-string
/// keys (int, python_value, ...) and the refined back-end are unchanged.
inline typet python_dict_key_elem_type(const typet &key_type)
{
  // Native: string keys are STRING-ID HANDLES (2026-07-21, completing the
  // representation invariant). The earlier POINTER box kept keys
  // fixed-width in the dict struct itself, but the pointed smt_string is
  // still byte-imaged whenever the key pointer enters an unresolved
  // value-set deref (observed: a nondet str parameter's truthiness deref
  // enumerating the key heap objects -- byte_extract of the pointed
  // string, the apigateway unpack_rec abort). A handle has no pointee:
  // nothing byte-granular is ever reachable from it.
  if(python_smt_string_native_flag() && is_python_string_type(key_type))
    return python_string_handle_type();
  return key_type;
}

/// Logical key type seeing THROUGH the native string-id handle (so
/// callers' is_python_string_type(...) key-type checks keep working
/// unchanged).
inline typet python_dict_logical_key_type(const typet &keys_elem_type)
{
  if(is_python_string_handle_type(keys_elem_type))
    return string_typet{};
  return keys_elem_type;
}

/// Read a key element through the handle indirection on native (the
/// denotation strtab(h)), identity otherwise. Use at every dict keys[i]
/// READ site so the rest of the code sees a string-typed key as before.
inline exprt python_dict_unbox_key(const exprt &key_elem)
{
  // HANDLE key: the denotation is strtab(h). The strtab symbol has a
  // fixed identity; the converter guarantees its symbol-table entry
  // whenever handles exist (strtab_symbol()).
  if(is_python_string_handle_type(key_elem.type()))
  {
    return function_application_exprt{
      symbol_exprt{
        "python::__cbmc_strtab",
        mathematical_function_typet{{signedbv_typet{64}}, string_typet{}}},
      {key_elem}};
  }
  return key_elem;
}

/// Lift container METADATA (an i64 length member) into the Python-int
/// domain of an index expression when the two differ:
/// --python-unbounded-ints makes Python ints integer_typet while
/// structural metadata stays signedbv[64] (the documented invariant at
/// python_list_type). Adjustments/comparisons must be built over
/// agreeing types (a mixed-type if_exprt aborts symex renaming); the
/// i64 -> integer direction is always value-exact, so lifting is sound.
inline exprt python_lift_to_index_domain(exprt e, const typet &idx_type)
{
  if(e.type() != idx_type)
    return typecast_exprt{std::move(e), idx_type};
  return e;
}

/// struct { int64 length; key_type keys[N]; value_type values[N]; }
inline struct_typet
python_dict_type(const typet &key_type, const typet &value_type)
{
  struct_typet::componentst components;
  components.push_back(struct_typet::componentt{"length", signedbv_typet{64}});
  components.push_back(struct_typet::componentt{
    "keys",
    array_typet{
      python_dict_key_elem_type(key_type),
      from_integer(PYTHON_MAX_DICT_SIZE, signedbv_typet{64})}});
  // Representation invariant (strings plan 2026-07-21): no variable-width
  // type in any aggregate -- an inline smt_string VALUE made dict[str, str]
  // variable-width and aborted smt2 byte-lowering on identity reads.
  // Values become string-id HANDLES under the native backend (keys are
  // boxed by python_dict_key_elem_type above).
  const typet stored_value_type =
    value_type.id() == ID_string ? python_string_handle_type() : value_type;
  components.push_back(struct_typet::componentt{
    "values",
    array_typet{
      stored_value_type,
      from_integer(PYTHON_MAX_DICT_SIZE, signedbv_typet{64})}});
  struct_typet result{components};
  result.set_tag("python_dict_array");
  return result;
}

/// Return the CBMC type used to represent Python list[T].
/// This is a struct { int64 length; T data[MAX_LIST_LENGTH]; }
inline struct_typet python_list_type(const typet &element_type_in)
{
  // Representation invariant: list[str] elements are string-id HANDLES
  // under the native backend. Three diagnosis rounds to get here:
  // (1) "regex intrinsics deliver into list slots" -- WRONG (findall/
  // split build their lists in Python stub code via append);
  // (2) handle-blind list-element COMPARISONS -- real, fixed by the
  // strtab-aware equality branch in convert_compare;
  // (3) a SECOND append emitter (python_converter_defs.cpp fast path)
  // typecast the value instead of coerce_element -- real, fixed by
  // routing it through the write choke point.
  const typet element_type = element_type_in.id() == ID_string
                               ? python_string_handle_type()
                               : element_type_in;
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
