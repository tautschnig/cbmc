/// \file
/// Shell language interface.

#ifndef CPROVER_SHELL_SHELL_LANGUAGE_H
#define CPROVER_SHELL_SHELL_LANGUAGE_H

#include <langapi/language.h>

#include "shell_ast.h"
#include "shell_converter.h"

/// Language interface for shell scripts (POSIX.1-2024 sh, with bash,
/// mksh, and zsh as dialects).
///
/// Parsing uses `shfmt --to-json` from mvdan/sh. The executable is taken
/// from the CBMC_SHFMT environment variable, or `shfmt` on the PATH.
class shell_languaget : public languaget
{
public:
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

  void set_language_options(const optionst &, message_handlert &) override;

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

  std::unique_ptr<languaget> new_language() override
  {
    return std::make_unique<shell_languaget>();
  }

  std::string id() const override
  {
    return "shell";
  }

  std::string description() const override
  {
    return "Shell (POSIX sh, bash, mksh, zsh)";
  }

  std::set<std::string> extensions() const override
  {
    return {"sh", "bash", "ksh", "mksh", "zsh"};
  }

  std::set<std::string> interpreters() const override
  {
    // ksh93 and other versioned names match "ksh" etc.
    return {
      "sh",
      "ash",
      "dash",
      "bash",
      "ksh",
      "mksh",
      "lksh",
      "pdksh",
      "posh",
      "yash",
      "zsh"};
  }

protected:
  shell_programt program;
  shell_convert_optionst convert_options;
  /// The --function option, which is not supported.
  std::string function;
};

std::unique_ptr<languaget> new_shell_language();

/// Print \p expr for traces: constant strings in shell quoting, shell
/// variables by name, and other expressions as C.
std::string expr2shell(const exprt &expr, const namespacet &ns);

#endif // CPROVER_SHELL_SHELL_LANGUAGE_H
