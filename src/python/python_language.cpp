/// \file
/// Python Language Interface

#include "python_language.h"

#include <util/config.h>
#include <util/get_base_name.h>
#include <util/message.h>
#include <util/run.h>
#include <util/symbol_table.h>
#include <util/tempfile.h>

#include <json/json_parser.h>

#include <fstream>

std::set<std::string> python_languaget::extensions() const
{
  return {"py"};
}

void python_languaget::modules_provided(std::set<std::string> &modules)
{
  modules.insert(get_base_name(parse_path, true));
}

void python_languaget::set_language_options(
  const optionst &,
  message_handlert &)
{
}

/// Find the ast_to_json.py script by searching relative to the source tree.
/// \return path to the script, or empty string if not found.
static std::string find_ast_script()
{
  const std::vector<std::string> search_paths = {
    "src/python/scripts/ast_to_json.py",
    "../src/python/scripts/ast_to_json.py",
    "../../src/python/scripts/ast_to_json.py",
  };

  for(const auto &candidate : search_paths)
  {
    std::ifstream test{candidate};
    if(test.good())
      return candidate;
  }

  return "";
}

bool python_languaget::parse(
  std::istream &,
  const std::string &path,
  message_handlert &message_handler)
{
  parse_path = path;

  messaget log{message_handler};

  std::string script_path = find_ast_script();
  if(script_path.empty())
  {
    log.error() << "Cannot find ast_to_json.py script" << messaget::eom;
    return true;
  }

  // Create temp file for JSON output
  temporary_filet json_file{"cbmc_python_ast_", ".json"};
  std::string json_path = json_file();

  // Run: python3 ast_to_json.py <input.py> <output.json>
  // argv[0] is the program name by convention
  int ret =
    run("python3", {"python3", script_path, path, json_path}, "", "", "");

  if(ret != 0)
  {
    log.error() << "Python parser failed for " << path << messaget::eom;
    return true;
  }

  // Read the JSON AST
  if(parse_json(json_path, message_handler, parse_tree.ast_json))
  {
    log.error() << "Failed to read JSON AST from " << json_path
                << messaget::eom;
    return true;
  }

  parse_tree.filename = path;

  return false;
}

bool python_languaget::typecheck(
  symbol_table_baset &,
  const std::string &,
  message_handlert &message_handler)
{
  messaget log{message_handler};
  log.error() << "Python typecheck not yet implemented" << messaget::eom;
  return true;
}

bool python_languaget::generate_support_functions(
  symbol_table_baset &,
  message_handlert &message_handler)
{
  messaget log{message_handler};
  log.error() << "Python support function generation not yet implemented"
              << messaget::eom;
  return true;
}

void python_languaget::show_parse(std::ostream &out, message_handlert &)
{
  parse_tree.output(out);
}

python_languaget::python_languaget() = default;
python_languaget::~python_languaget() = default;

std::unique_ptr<languaget> new_python_language()
{
  return std::make_unique<python_languaget>();
}

bool python_languaget::from_expr(
  const exprt &,
  std::string &code,
  const namespacet &)
{
  // TODO: implement expr2python
  code = "(python expression)";
  return false;
}

bool python_languaget::from_type(
  const typet &,
  std::string &code,
  const namespacet &)
{
  // TODO: implement type2python
  code = "(python type)";
  return false;
}

bool python_languaget::type_to_name(
  const typet &,
  std::string &name,
  const namespacet &)
{
  // TODO: implement
  name = "(python type name)";
  return false;
}

bool python_languaget::to_expr(
  const std::string &,
  const std::string &,
  exprt &,
  const namespacet &,
  message_handlert &)
{
  return true;
}
