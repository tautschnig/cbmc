/*******************************************************************\

Module: Go front-end: in-memory Go AST

Author: Michael Tautschnig

\*******************************************************************/

/// \file
/// The in-memory Go AST consumed by the converter (go_convert.h).
///
/// Every node is an `irept` whose id is the node kind, named after the
/// `go/ast` type ("BinaryExpr", "FuncDecl", ...). Fields keep their `go/ast`
/// names: scalar fields are `irep_idt` values (`n.get("Op")`), sub-nodes are
/// named sub-trees (`n.find("X")`), and lists are `list` ireps whose
/// `get_sub()` holds the elements. Type-checker results use the reserved
/// fields listed below, and positions are stored as `source_locationt`.
///
/// The AST is currently loaded from the JSON produced by `cbmc-go-helper`
/// (helper/main.go), which takes all semantic information from `go/types`.
/// A native parser and type checker can produce the same tree instead
/// (doc/architectural/go-frontend-plan.md §4.3).
///
/// Reserved fields (all optional):
///   `_t`    index into go_parse_treet::types of the expression's type
///   `_cv`   exact constant value, with `_cvk` its kind (bool, int, ...)
///   `_tx`   "1" if the expression denotes a type
///   `_obj`  id (key of go_parse_treet::objects) of an identifier's object;
///           `_def` is "1" if the identifier defines it
///   `_inst` list of type indices: inferred or explicit type arguments
///   `_impl` id of an implicitly declared object (type-switch clauses)
///   `_sel`  selection: `kind`, `path` (list), `indirect`, `obj`

#ifndef CPROVER_GO_GO_PARSE_TREE_H
#define CPROVER_GO_GO_PARSE_TREE_H

#include <util/irep.h>
#include <util/source_location.h>

#include <iosfwd>
#include <map>
#include <string>
#include <vector>

class jsont;
class message_handlert;

/// A Go package: its files' syntax trees plus the type checker's tables.
class go_parse_treet
{
public:
  /// Toolchain that produced the tree, e.g. "go1.27.3".
  std::string go_version;
  /// Target architecture the package was type-checked for, e.g. "amd64".
  std::string goarch;
  /// Package path, "main" for a program.
  std::string package;
  /// Absolute paths of the package's files, indexed by position records.
  std::vector<std::string> files;
  /// One "File" node per file.
  std::vector<irept> asts;
  /// Type table; each entry has `k` (basic, named, pointer, ...).
  std::vector<irept> types;
  /// Objects (variables, functions, constants, ...) by id.
  std::map<irep_idt, irept> objects;

  /// One package-level variable initialisation, in the order mandated by
  /// Go1.27 §Package_initialization (computed by go/types).
  struct initializert
  {
    std::vector<irep_idt> lhs;
    irept rhs;
  };
  /// A package converted from source: the program's, a user package it
  /// imports, a model of a standard-library package, or the runtime.
  struct packaget
  {
    std::string path;
    std::vector<initializert> init_order;
  };
  /// The packages, in the order of their initialisation.
  std::vector<packaget> packages;

  /// Load the helper's JSON (schema version 1).
  /// \return true on error
  bool load_json(const jsont &json, message_handlert &message_handler);

  void clear();
  void output(std::ostream &out) const;

  /// The type-table entry for index \p index (as stored in `_t`).
  const irept &type_entry(const irep_idt &index) const;
  /// The object with id \p id; nil if unknown.
  const irept &object(const irep_idt &id) const;
};

/// The named sub-node \p field of \p node, or nil.
inline const irept &go_field(const irept &node, const irep_idt &field)
{
  return node.find(field);
}

/// The elements of list field \p field of \p node (empty if absent).
inline const irept::subt &go_list(const irept &node, const irep_idt &field)
{
  return node.find(field).get_sub();
}

/// The source location of \p node.
inline const source_locationt &go_location(const irept &node)
{
  return static_cast<const source_locationt &>(node.find(ID_C_source_location));
}

#endif // CPROVER_GO_GO_PARSE_TREE_H
