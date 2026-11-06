/*******************************************************************\

Module: Go front-end: language interface

Author: Michael Tautschnig

\*******************************************************************/

/// \file
/// The Go language interface (`languaget`).

#ifndef CPROVER_GO_GO_LANGUAGE_H
#define CPROVER_GO_GO_LANGUAGE_H

#include <langapi/language.h>

#include "go_convert.h"
#include "go_parse_tree.h"

#include <optional>

/// Go front-end: parsing and type checking by `cbmc-go-helper` (go/parser
/// and go/types), conversion by go_convertert. See
/// doc/architectural/go-frontend-plan.md.
class go_languaget : public languaget
{
public:
  void
  set_language_options(const optionst &options, message_handlert &) override;

  bool parse(
    std::istream &instream,
    const std::string &path,
    message_handlert &message_handler) override;

  bool generate_support_functions(
    symbol_table_baset &symbol_table,
    message_handlert &message_handler) override;

  bool typecheck(
    symbol_table_baset &symbol_table,
    const std::string &module,
    message_handlert &message_handler) override;

  void show_parse(std::ostream &out, message_handlert &) override;

  bool from_expr(const exprt &expr, std::string &code, const namespacet &ns)
    override;
  bool from_type(const typet &type, std::string &code, const namespacet &ns)
    override;
  bool type_to_name(const typet &type, std::string &name, const namespacet &ns)
    override;
  bool to_expr(
    const std::string &code,
    const std::string &module,
    exprt &expr,
    const namespacet &ns,
    message_handlert &message_handler) override;

  std::unique_ptr<languaget> new_language() override
  {
    return std::make_unique<go_languaget>();
  }

  std::string id() const override
  {
    return "go";
  }
  std::string description() const override
  {
    return "Go";
  }
  std::set<std::string> extensions() const override
  {
    return {"go"};
  }

  /// The GOARCH matching CBMC's configured target (config.ansi_c), or
  /// empty if Go has no port for it (Go1.27 §Numeric_types: int widths
  /// must agree between go/types and config.go).
  static std::string target_goarch();

protected:
  /// Run the helper on the files parsed so far (see pending_files in
  /// go_language.cpp), if any, and load its output.
  /// \return true on error
  bool load_pending(message_handlert &message_handler);

  go_parse_treet parse_tree;
  /// The function named by --function, or empty for main.main.
  std::string function_entry_point;
  /// The compiler named by --go-compiler, or empty to use the one detected.
  std::string go_compiler;
  /// Whether deadlocks are reported (not with --go-no-deadlock-check).
  bool deadlock_check = true;
  bool race_check = false;
  /// Whether the helper keeps the functions of imported packages that the
  /// program cannot reach (--go-keep-unreachable; it prunes them otherwise).
  bool keep_unreachable = false;
  /// Bounds of arbitrary values with references (--max-nondet-tree-depth,
  /// --min-null-tree-depth).
  object_factory_parameterst nondet_parameters;

  /// The capacities of append for go_compiler, or for the compiler the
  /// helper detected (see append_capacity in go_language.cpp).
  /// \return empty on error
  std::optional<go_append_capacityt>
  append_capacity(message_handlert &message_handler) const;
};

std::unique_ptr<languaget> new_go_language();

#endif // CPROVER_GO_GO_LANGUAGE_H
