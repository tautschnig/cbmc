/*******************************************************************\

Module: Go front-end: Go types to CBMC types

Author: Michael Tautschnig

\*******************************************************************/

/// \file
/// Mapping of go/types type-table entries to CBMC `typet`s.

#ifndef CPROVER_GO_GO_TYPES_H
#define CPROVER_GO_GO_TYPES_H

#include <util/irep.h>
#include <util/type.h>

#include <string>

class go_parse_treet;

/// Name of the comment recording a type's Go spelling (for output).
#define ID_C_go_type "#go_type"
/// Name of the comment marking the struct that holds a tuple of values.
#define ID_C_go_tuple "#go_tuple"

/// True if \p type holds the values of a multi-valued expression (see
/// go_type_convertert::tuple).
inline bool is_tuple(const typet &type)
{
  return type.id() == ID_struct && type.get_bool(ID_C_go_tuple);
}

/// Converts entries of go_parse_treet::types. Unsupported types yield a
/// nil type; `unsupported_reason` then says why.
class go_type_convertert
{
public:
  explicit go_type_convertert(const go_parse_treet &parse_tree)
    : parse_tree(parse_tree)
  {
  }

  /// The CBMC type of values of type-table index \p index. Function
  /// types yield pointers to code (function values).
  typet operator()(const irep_idt &index);

  /// The code type of the function signature at index \p index, for
  /// declaring functions. Several results are returned as one struct, see
  /// tuple().
  typet signature(const irep_idt &index);

  /// The struct type holding the values of a tuple (the results of a
  /// function with several results), with components $0, $1, ...
  typet tuple(const irept::subt &items);

  /// For a nil result of operator(): a description of what is unsupported.
  std::string unsupported_reason;

  /// True if \p index refers to an untyped type ("untyped int", ...).
  bool is_untyped(const irep_idt &index) const;
  /// True if \p index refers to a type whose core is a signed integer.
  bool is_signed_integer(const irep_idt &index) const;
  /// The basic kind ("int8", "bool", ...) underlying \p index, or empty.
  irep_idt basic_kind(const irep_idt &index) const;

private:
  const go_parse_treet &parse_tree;

  /// Follow named types to their underlying type-table entry.
  const irept &underlying(const irep_idt &index) const;
  typet basic(const irep_idt &kind);
};

#endif // CPROVER_GO_GO_TYPES_H
