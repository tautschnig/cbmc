/*******************************************************************\

Module: COBOL Language Interface

Author: Kiro

\*******************************************************************/

/// \file
/// COBOL Language Interface
///
/// Implements the \ref languaget interface for a subset of IBM Enterprise
/// COBOL. See doc/architectural/cobol-frontend-plan.md for scope and design.

#ifndef CPROVER_COBOL_COBOL_LANGUAGE_H
#define CPROVER_COBOL_COBOL_LANGUAGE_H

#include <langapi/language.h>

#include "cobol_scanner.h"

#include <string>
#include <vector>

/// Mode string identifying COBOL symbols in the symbol table.
#define COBOL_MODE "cobol"

class cobol_languaget : public languaget
{
public:
  bool parse(
    std::istream &instream,
    const std::string &path,
    message_handlert &message_handler) override;

  void set_language_options(const optionst &, message_handlert &) override;

  bool typecheck(
    symbol_table_baset &symbol_table,
    const std::string &module,
    message_handlert &message_handler) override;

  bool generate_support_functions(
    symbol_table_baset &symbol_table,
    message_handlert &message_handler) override;

  void
  show_parse(std::ostream &out, message_handlert &message_handler) override;

  bool from_expr(const exprt &expr, std::string &code, const namespacet &ns)
    override;

  bool from_type(const typet &type, std::string &code, const namespacet &ns)
    override;

  bool to_expr(
    const std::string &code,
    const std::string &module,
    exprt &expr,
    const namespacet &ns,
    message_handlert &message_handler) override;

  std::unique_ptr<languaget> new_language() override;

  std::string id() const override
  {
    return COBOL_MODE;
  }

  std::string description() const override
  {
    return "COBOL";
  }

  std::set<std::string> extensions() const override;

  void modules_provided(std::set<std::string> &modules) override;

  cobol_languaget() = default;
  ~cobol_languaget() override = default;

private:
  std::vector<cobol_tokent> tokens;
  std::string parse_path;
  /// Emit implicit runtime-property checks (subscript / reference-modification
  /// range). Mirrors CBMC's "bounds-check" option (on by default in v6+, off
  /// under --no-standard-checks). See doc/architectural/cobol-runtime-checks.md.
  bool runtime_checks = true;
  /// Emit the division-by-zero check. Mirrors CBMC's "div-by-zero-check"
  /// option (on by default in v6+, off under --no-standard-checks).
  bool div_checks = true;
};

std::unique_ptr<languaget> new_cobol_language();

#endif // CPROVER_COBOL_COBOL_LANGUAGE_H
