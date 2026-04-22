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

#include "python_converter.h"

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

/// The Python code that converts a .py file to a JSON AST.
/// Invoked as: python3 -c '<this code>' <input.py> <output.json>
// clang-format off
#define PYTHON_AST_TO_JSON_CODE \
  "import ast,json,sys\n" \
  "def c(n):\n" \
  " if isinstance(n,ast.AST):\n" \
  "  r={'_type':n.__class__.__name__}\n" \
  "  for f,v in ast.iter_fields(n):r[f]=c(v)\n" \
  "  for a in('lineno','col_offset','end_lineno','end_col_offset'):\n" \
  "   v=getattr(n,a,None)\n" \
  "   if v is not None:r[a]=v\n" \
  "  return r\n" \
  " if isinstance(n,list):return[c(x)for x in n]\n" \
  " return n\n" \
  "t=ast.parse(open(sys.argv[1]).read(),sys.argv[1])\n" \
  "r=c(t);r['_filename']=sys.argv[1]\n" \
  "json.dump(r,open(sys.argv[2],'w'),default=str)\n"
// clang-format on

bool python_languaget::parse(
  std::istream &,
  const std::string &path,
  message_handlert &message_handler)
{
  parse_path = path;

  messaget log{message_handler};

  // Create temp file for JSON output
  temporary_filet json_file{"cbmc_python_ast_", ".json"};
  std::string json_path = json_file();

  // Invoke python3 -c '<inline script>' <input.py> <output.json>
  // This is analogous to how the C front-end invokes gcc -E.
  int ret = run(
    "python3",
    {"python3", "-c", PYTHON_AST_TO_JSON_CODE, path, json_path},
    "",
    "",
    "");

  if(ret != 0)
  {
    log.error() << "Python AST generation failed for " << path
                << " (is python3 installed?)" << messaget::eom;
    return true;
  }

  // Read the JSON AST
  if(parse_json(json_path, message_handler, parse_tree.ast_json))
  {
    log.error() << "Failed to read Python JSON AST" << messaget::eom;
    return true;
  }

  parse_tree.filename = path;

  return false;
}

bool python_languaget::typecheck(
  symbol_table_baset &symbol_table,
  const std::string &,
  message_handlert &message_handler)
{
  python_convertert converter{symbol_table, parse_tree, message_handler};
  return converter.convert();
}

bool python_languaget::generate_support_functions(
  symbol_table_baset &,
  message_handlert &)
{
  // __CPROVER__start is created by the converter
  return false;
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
