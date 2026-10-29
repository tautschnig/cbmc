/*******************************************************************\

Module: Go front-end: Go types to CBMC types

Author: Michael Tautschnig

\*******************************************************************/

/// \file
/// Mapping of go/types type-table entries to CBMC `typet`s.

#ifndef CPROVER_GO_GO_TYPES_H
#define CPROVER_GO_GO_TYPES_H

#include <util/type.h>

#include <string>

class go_parse_treet;

/// Name of the comment recording a type's Go spelling (for output).
#define ID_C_go_type "#go_type"

/// Converts entries of go_parse_treet::types. Unsupported types yield a
/// nil type; `unsupported_reason` then says why.
class go_type_convertert
{
public:
  explicit go_type_convertert(const go_parse_treet &parse_tree)
    : parse_tree(parse_tree)
  {
  }

  /// The CBMC type for type-table index \p index.
  typet operator()(const irep_idt &index);

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
