/// \file
/// Shell language interface.

#include "shell_language.h"

#include <util/arith_tools.h>
#include <util/cprover_prefix.h>
#include <util/json.h>
#include <util/message.h>
#include <util/namespace.h>
#include <util/options.h>
#include <util/run.h>
#include <util/std_code.h>
#include <util/string2int.h>
#include <util/symbol_table_base.h>
#include <util/tempfile.h>

#include <goto-programs/goto_functions.h>
#include <goto-programs/goto_instruction_code.h>

#include <ansi-c/expr2c.h>
#include <json/json_parser.h>

#include "shell_converter.h"
#include "shell_runtime.h"
#include "shfmt_reader.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

std::unique_ptr<languaget> new_shell_language()
{
  return std::make_unique<shell_languaget>();
}

namespace
{
/// The shfmt executable: $CBMC_SHFMT, or `shfmt` on the PATH.
std::string shfmt_executable()
{
  const char *from_env = std::getenv("CBMC_SHFMT");
  if(from_env != nullptr && *from_env != '\0')
    return from_env;
  return "shfmt";
}
} // namespace

bool shell_languaget::parse(
  std::istream &instream,
  const std::string &path,
  message_handlert &message_handler)
{
  messaget log{message_handler};

  // Read the script's exact bytes: the reader recovers the raw text of
  // arithmetic expansions from shfmt's byte offsets.
  std::ostringstream buffer;
  buffer << instream.rdbuf();
  const std::string source = buffer.str();

  program = shell_programt{};
  program.dialect = detect_shell_dialect(source, path);

  // shfmt's --to-json reads only standard input.
  temporary_filet source_file{"cbmc_shell_src_", ".sh"};
  {
    std::ofstream out{source_file(), std::ios::binary};
    out << source;
  }
  temporary_filet json_file{"cbmc_shell_ast_", ".json"};
  temporary_filet stderr_file{"cbmc_shell_err_", ".txt"};

  const std::string shfmt = shfmt_executable();
  const int ret = run(
    shfmt,
    {shfmt, "-ln", shfmt_dialect_name(program.dialect), "--to-json"},
    source_file(),
    json_file(),
    stderr_file());

  if(ret != 0)
  {
    // shfmt reports "line:col: message"; prefix the script's path.
    std::ifstream errors{stderr_file()};
    std::string line;
    while(std::getline(errors, line))
      log.error() << path << ':' << line << messaget::eom;
    if(ret == 127 || ret == -1)
    {
      log.error() << "failed to run '" << shfmt
                  << "'. The shell front-end requires shfmt "
                     "(https://github.com/mvdan/sh); set CBMC_SHFMT to its "
                     "path if it is not on PATH."
                  << messaget::eom;
    }
    return true;
  }

  jsont json;
  if(parse_json(json_file(), message_handler, json))
  {
    log.error() << "failed to read shfmt's JSON output" << messaget::eom;
    return true;
  }

  return shfmt_json_to_shell_program(
    json, source, path, program, message_handler);
}

bool shell_languaget::typecheck(
  symbol_table_baset &symbol_table,
  const std::string &,
  message_handlert &message_handler)
{
  return shell_convert(
    program, max_string_length, symbol_table, message_handler);
}

void shell_languaget::set_language_options(
  const optionst &options,
  message_handlert &message_handler)
{
  function = options.get_option("function");
  const std::string max = options.get_option("shell-max-string-length");
  if(max.empty())
    return;
  const auto value = string2optional_size_t(max);
  // Integers need up to 20 bytes, and $((...)) must be representable.
  if(!value.has_value() || *value < 24 || *value > 100000)
  {
    messaget log{message_handler};
    log.error() << "--shell-max-string-length must be a number in [24, "
                   "100000]; using the default "
                << SHELL_DEFAULT_MAX_STRING_LENGTH << messaget::eom;
    return;
  }
  max_string_length = *value;
}

bool shell_languaget::generate_support_functions(
  symbol_table_baset &symbol_table,
  message_handlert &message_handler)
{
  if(symbol_table.has_symbol(goto_functionst::entry_point()))
    return false;
  if(!function.empty())
  {
    messaget log{message_handler};
    log.error() << "--function is not supported for shell scripts"
                << messaget::eom;
    return true;
  }

  const symbolt &init = symbol_table.lookup_ref(CPROVER_PREFIX "initialize");
  const symbolt &main = symbol_table.lookup_ref(SHELL_MAIN_FUNCTION);
  code_blockt body;
  body.add(code_labelt{CPROVER_PREFIX "HIDE", code_skipt{}});
  body.add(code_function_callt{init.symbol_expr()});
  code_function_callt call_main{main.symbol_expr()};
  call_main.add_source_location() = main.location;
  body.add(std::move(call_main));

  symbolt start{
    goto_functionst::entry_point(),
    code_typet{{}, empty_typet{}},
    shell_mode()};
  start.base_name = goto_functionst::entry_point();
  start.pretty_name = goto_functionst::entry_point();
  start.value = std::move(body);
  return !symbol_table.insert(std::move(start)).second;
}

void shell_languaget::show_parse(std::ostream &out, message_handlert &)
{
  program.output(out);
}

bool shell_languaget::from_expr(
  const exprt &expr,
  std::string &code,
  const namespacet &ns)
{
  code = expr2shell(expr, ns);
  return false;
}

bool shell_languaget::from_type(
  const typet &type,
  std::string &code,
  const namespacet &ns)
{
  code = type2c(type, ns);
  return false;
}

bool shell_languaget::to_expr(
  const std::string &,
  const std::string &,
  exprt &,
  const namespacet &,
  message_handlert &message_handler)
{
  messaget log{message_handler};
  log.error() << "the shell front-end does not support parsing expressions"
              << messaget::eom;
  return true;
}

std::string expr2shell(const exprt &expr, const namespacet &ns)
{
  // A constant string, as in a trace: 'text', or <unset>.
  if(
    expr.id() == ID_struct && expr.type().id() == ID_struct_tag &&
    to_struct_tag_type(expr.type()).get_identifier() == "tag-" SHELL_STRING_TAG)
  {
    const struct_typet &type = ns.follow_tag(to_struct_tag_type(expr.type()));
    if(type.components().size() != expr.operands().size())
      return expr2c(expr, ns);
    const auto &operands = expr.operands();
    const auto set =
      numeric_cast<mp_integer>(operands[type.component_number("set")]);
    const auto length =
      numeric_cast<mp_integer>(operands[type.component_number("length")]);
    const exprt &data = operands[type.component_number("data")];
    if(set.has_value() && *set == 0)
      return "<unset>";
    if(
      length.has_value() && data.id() == ID_array &&
      *length <= data.operands().size())
    {
      std::string text;
      for(std::size_t i = 0; i < length->to_ulong(); ++i)
      {
        const auto c = numeric_cast<mp_integer>(data.operands()[i]);
        if(!c.has_value())
          return expr2c(expr, ns);
        text += static_cast<char>(c->to_ulong());
      }
      // Single quotes, as the shell would need to read the value back.
      std::string quoted = "'";
      for(char c : text)
        quoted += c == '\'' ? std::string{"'\\''"} : std::string{c};
      return quoted + "'";
    }
  }
  if(expr.id() == ID_symbol)
  {
    const symbolt *symbol = nullptr;
    if(
      !ns.lookup(to_symbol_expr(expr).get_identifier(), symbol) &&
      symbol->mode == shell_mode())
    {
      return id2string(symbol->base_name);
    }
  }
  return expr2c(expr, ns);
}
