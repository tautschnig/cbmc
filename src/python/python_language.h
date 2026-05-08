/// \file
/// Python Language Interface

#ifndef CPROVER_PYTHON_PYTHON_LANGUAGE_H
#define CPROVER_PYTHON_PYTHON_LANGUAGE_H

#include <langapi/language.h>

#include "python_parse_tree.h"
#include "python_types.h"

/// Implements the language interface for Python.
class python_languaget : public languaget
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

  ~python_languaget() override;
  python_languaget();

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
    return std::make_unique<python_languaget>();
  }

  std::string id() const override
  {
    return "python";
  }

  std::string description() const override
  {
    return "Python 3";
  }

  std::set<std::string> extensions() const override;

  void modules_provided(std::set<std::string> &modules) override;

protected:
  python_parse_treet parse_tree;
  std::string parse_path;

  /// The function name specified by --function, or empty.
  std::string function_entry_point;

  /// Whether to use mathematical integers instead of int64.
  bool unbounded_ints = false;
  bool no_body_check = false;
  bool python_strict_warnings = false;
  std::size_t max_string_length = PYTHON_MAX_STRING_LENGTH;
  std::size_t max_list_length = PYTHON_MAX_LIST_LENGTH;

  /// Search paths for module resolution (from PYTHONPATH + source dir)
  std::vector<std::string> python_paths;

  /// Already-parsed module ASTs (to avoid re-parsing)
  std::map<std::string, jsont> parsed_modules;

  /// Resolve and parse a Python module, returning its AST JSON.
  /// Returns nullptr if not found.
  const jsont *
  resolve_module(const std::string &module_name, message_handlert &handler);
};

std::unique_ptr<languaget> new_python_language();

#endif // CPROVER_PYTHON_PYTHON_LANGUAGE_H
