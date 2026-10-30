/*******************************************************************\

Module: Go front-end: Go types to CBMC types

Author: Michael Tautschnig

\*******************************************************************/

/// \file
/// Mapping of go/types type-table entries to CBMC `typet`s.

#ifndef CPROVER_GO_GO_TYPES_H
#define CPROVER_GO_GO_TYPES_H

#include <util/irep.h>
#include <util/pointer_expr.h>
#include <util/std_types.h>

#include <set>
#include <string>

class go_parse_treet;
class symbol_table_baset;

/// Name of the comment recording a type's Go spelling (for output).
#define ID_C_go_type "#go_type"
/// Name of the comment marking the struct that holds a tuple of values.
#define ID_C_go_tuple "#go_tuple"

/// Name of the comment marking the struct that represents a function value.
#define ID_C_go_func_value "#go_func_value"
/// Name of the comment marking the struct that represents a string.
#define ID_C_go_string "#go_string"
/// Name of the comment marking the struct that represents a slice.
#define ID_C_go_slice "#go_slice"
/// Name of the comment marking the header struct of maps.
#define ID_C_go_map "#go_map"
/// Name of the comment marking the struct that represents a complex number.
#define ID_C_go_complex "#go_complex"

/// True if \p type holds the values of a multi-valued expression (see
/// go_type_convertert::tuple).
inline bool is_tuple(const typet &type)
{
  return type.id() == ID_struct && type.get_bool(ID_C_go_tuple);
}

/// True if \p type is that of function values (see
/// go_type_convertert::func_value).
inline bool is_func_value(const typet &type)
{
  return type.id() == ID_struct && type.get_bool(ID_C_go_func_value);
}

/// True if \p type is that of strings (see go_type_convertert::string_type).
inline bool is_go_string(const typet &type)
{
  return type.id() == ID_struct && type.get_bool(ID_C_go_string);
}

/// True if \p type is that of slices (see go_type_convertert::slice_type).
inline bool is_go_slice(const typet &type)
{
  return type.id() == ID_struct && type.get_bool(ID_C_go_slice);
}

/// True if \p type is that of complex numbers (see
/// go_type_convertert::complex_type).
inline bool is_go_complex(const typet &type)
{
  return type.id() == ID_struct && type.get_bool(ID_C_go_complex);
}

/// True if \p type is that of maps (see go_type_convertert::map_type).
inline bool is_go_map(const typet &type)
{
  return type.id() == ID_pointer &&
         to_pointer_type(type).base_type().get_bool(ID_C_go_map);
}

/// The CBMC symbol for the Go object with id \p object_id.
/// Package-level objects ("main.f") become "go::main.f"; local objects,
/// whose ids carry a uniqueness suffix ("main.x#3"), become
/// "go::main::3::x", so that symbol names end in their base name.
irep_idt go_symbol_id(const irep_idt &object_id);

/// Converts entries of go_parse_treet::types. Unsupported types yield a
/// nil type; `unsupported_reason` then says why.
class go_type_convertert
{
public:
  go_type_convertert(
    const go_parse_treet &parse_tree,
    symbol_table_baset &symbol_table)
    : parse_tree(parse_tree), symbol_table(symbol_table)
  {
  }

  /// The CBMC type of values of type-table index \p index. Named struct
  /// types become struct tags, whose type symbols are added on first use.
  /// Function types yield function values, see func_value().
  typet operator()(const irep_idt &index);

  /// The code type of the function signature at index \p index, for
  /// declaring functions. Several results are returned as one struct, see
  /// tuple().
  typet signature(const irep_idt &index);

  /// The struct type holding the values of a tuple (the results of a
  /// function with several results), with components $0, $1, ...
  typet tuple(const irept::subt &items);

  /// The type of function values with code type \p code (from signature):
  /// a struct of a pointer to code that takes an environment pointer
  /// ($env) before the parameters, and that environment pointer. The
  /// environment holds the variables a closure captures (Go1.27
  /// §Function_literals); nil is the value whose code pointer is null.
  static struct_typet func_value(const code_typet &code);

  /// The type of the environment pointer of function values.
  static pointer_typet env_pointer_type();

  /// The type of strings: a pointer to the first byte and the length,
  /// `{data *uint8, len int}`. Go1.27 §String_types: "A string value is a
  /// (possibly empty) sequence of bytes." "Strings are immutable", so
  /// strings may share their bytes. The zero value, "", has a null data
  /// pointer.
  static struct_typet string_type();

  /// The type of slices of \p element: `{data *E, len int, cap int}`.
  /// Go1.27 §Slice_types: "A slice is a descriptor for a contiguous segment
  /// of an underlying array". The zero value, nil, has a null data pointer.
  static struct_typet slice_type(const typet &element);

  /// The type of maps from \p key to \p value: a pointer to a header
  /// `{len int, cap int, keys *K, vals *V}` (go_maps.cpp). Go1.27
  /// §Map_types: "The value of an uninitialized map is nil."
  static pointer_typet map_type(const typet &key, const typet &value);

  /// The type of complex numbers with parts of floating-point type \p part:
  /// `{real, imag}`.
  static struct_typet complex_type(const typet &part);

  /// For a nil result of operator(): a description of what is unsupported.
  std::string unsupported_reason;

  /// True if \p index refers to an untyped type ("untyped int", ...).
  bool is_untyped(const irep_idt &index) const;
  /// True if \p index refers to a type whose core is a signed integer.
  bool is_signed_integer(const irep_idt &index) const;
  /// The basic kind ("int8", "bool", ...) underlying \p index, or empty.
  irep_idt basic_kind(const irep_idt &index) const;

  /// Follow named types to their underlying type-table entry.
  const irept &underlying(const irep_idt &index) const;

private:
  const go_parse_treet &parse_tree;
  symbol_table_baset &symbol_table;
  /// Named non-struct types being converted, to detect types that refer
  /// to themselves other than through a struct (e.g., type P *P), which
  /// CBMC types cannot represent.
  std::set<irep_idt> converting;

  typet basic(const irep_idt &kind);
  typet struct_type(const irept &entry);
  typet named_struct(const irep_idt &index, const irept &entry);
};

#endif // CPROVER_GO_GO_TYPES_H
