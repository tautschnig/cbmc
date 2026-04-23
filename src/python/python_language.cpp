/// \file
/// Python Language Interface

#include "python_language.h"

#include <util/c_types.h>
#include <util/config.h>
#include <util/cprover_prefix.h>
#include <util/get_base_name.h>
#include <util/message.h>
#include <util/options.h>
#include <util/run.h>
#include <util/std_code.h>
#include <util/std_expr.h>
#include <util/symbol_table.h>
#include <util/tempfile.h>

#include <json/json_parser.h>

#include "expr2python.h"
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
  const optionst &options,
  message_handlert &)
{
  function_entry_point = options.get_option("function");
  unbounded_ints = options.get_bool_option("python-unbounded-ints");
}

/// The Python code that converts a .py file to a JSON AST.
/// Invoked as: python3 -c '<this code>' <input.py> <output.json>
// clang-format off
#define PYTHON_AST_TO_JSON_CODE \
  "import sys\n" \
  "try:\n" \
  " import ast,json\n" \
  "except ImportError as e:\n" \
  " print('CBMC Python front-end: failed to import required Python' \\\n" \
  "  ' module: '+str(e)+'\\nPlease ensure Python 3 is installed with' \\\n" \
  "  ' its standard library (the ast and json modules are required).',\\\n" \
  "  file=sys.stderr);sys.exit(1)\n" \
  "try:\n" \
  " t=ast.parse(open(sys.argv[1]).read(),sys.argv[1])\n" \
  "except SyntaxError as e:\n" \
  " print(str(e),file=sys.stderr);sys.exit(1)\n" \
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
  temporary_filet stderr_file{"cbmc_python_err_", ".txt"};
  std::string stderr_path = stderr_file();

  int ret = run(
    "python3",
    {"python3", "-c", PYTHON_AST_TO_JSON_CODE, path, json_path},
    "",
    "",
    stderr_path);

  if(ret != 0)
  {
    // Show any error output from Python
    std::ifstream stderr_stream{stderr_path};
    if(stderr_stream)
    {
      std::string line;
      while(std::getline(stderr_stream, line))
        log.error() << line << messaget::eom;
    }

    if(ret == 127 || ret == -1)
    {
      log.error() << "Failed to run python3. CBMC's Python front-end requires "
                     "Python 3 to be installed and available on PATH."
                  << messaget::eom;
    }

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
  converter.set_unbounded_ints(unbounded_ints);
  return converter.convert();
}

bool python_languaget::generate_support_functions(
  symbol_table_baset &symbol_table,
  message_handlert &message_handler)
{
  // If __CPROVER__start already exists, nothing to do
  const std::string start_name = std::string{CPROVER_PREFIX} + "_start";
  irep_idt start_id{start_name};
  if(symbol_table.lookup(start_id) != nullptr)
    return false;

  messaget log{message_handler};
  code_blockt start_body;

  if(!function_entry_point.empty())
  {
    // --function mode: generate a harness that calls the specified function
    // with nondet arguments of the annotated types.
    irep_idt func_id{"python::" + function_entry_point};
    const symbolt *func_sym = symbol_table.lookup(func_id);
    if(func_sym == nullptr)
    {
      log.error() << "Function '" << function_entry_point << "' not found"
                  << messaget::eom;
      return true;
    }

    const code_typet &func_type = to_code_type(func_sym->type);

    // Generate nondet arguments
    exprt::operandst arguments;
    for(const auto &param : func_type.parameters())
    {
      side_effect_expr_nondett nondet{param.type(), source_locationt{}};
      arguments.push_back(std::move(nondet));
    }

    // Call the function
    if(func_type.return_type().id() == ID_empty)
    {
      // void function: just call it
      side_effect_expr_function_callt call{
        func_sym->symbol_expr(),
        std::move(arguments),
        func_type.return_type(),
        source_locationt{}};
      start_body.add(code_expressiont{call});
    }
    else
    {
      // Non-void: call and discard result
      side_effect_expr_function_callt call{
        func_sym->symbol_expr(),
        std::move(arguments),
        func_type.return_type(),
        source_locationt{}};
      start_body.add(code_expressiont{call});
    }
  }
  else
  {
    // Default mode: use the module body as the entry point
    const symbolt *module_body_sym =
      symbol_table.lookup("python::__module_body");
    if(module_body_sym != nullptr && !module_body_sym->value.is_nil())
      start_body = to_code_block(to_code(module_body_sym->value));
  }

  code_typet start_type{{}, empty_typet{}};
  symbolt start_symbol{start_id, start_type, "python"};
  start_symbol.base_name = start_name;
  start_symbol.is_lvalue = true;
  start_symbol.value = start_body;

  symbol_table.add(start_symbol);

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
  const exprt &expr,
  std::string &code,
  const namespacet &ns)
{
  code = expr2python(expr, ns);
  return false;
}

bool python_languaget::from_type(
  const typet &type,
  std::string &code,
  const namespacet &ns)
{
  code = type2python(type, ns);
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
