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
#include "shfmt_parser.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

std::unique_ptr<languaget> new_shell_language()
{
  return std::make_unique<shell_languaget>();
}

namespace
{
/// Set \p value from option \p name if it is given and in [min, max].
void read_bound(
  const optionst &options,
  const std::string &name,
  std::size_t min,
  std::size_t max,
  std::size_t &value,
  message_handlert &message_handler)
{
  const std::string text = options.get_option(name);
  if(text.empty())
    return;
  const auto parsed = string2optional_size_t(text);
  if(!parsed.has_value() || *parsed < min || *parsed > max)
  {
    messaget log{message_handler};
    log.error() << "--" << name << " must be a number in [" << min << ", "
                << max << "]; using the default " << value << messaget::eom;
    return;
  }
  value = *parsed;
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
  const auto options = shebang_options(source);
  if(!options.has_value())
  {
    log.error() << path
                << ": unsupported arguments on the #! line (only single-letter "
                   "options such as -eu are supported)"
                << messaget::eom;
    return true;
  }
  program.initial_options = *options;

  return parse_shell_source(source, path, program, message_handler);
}

bool shell_languaget::typecheck(
  symbol_table_baset &symbol_table,
  const std::string &,
  message_handlert &message_handler)
{
  return shell_convert(program, convert_options, symbol_table, message_handler);
}

void shell_languaget::set_language_options(
  const optionst &options,
  message_handlert &message_handler)
{
  function = options.get_option("function");
  // Integers need up to 20 bytes, and $((...)) must be representable.
  read_bound(
    options,
    "shell-max-string-length",
    24,
    1u << 26,
    convert_options.max_string_length,
    message_handler);
  read_bound(
    options,
    "shell-max-arguments",
    1,
    1000,
    convert_options.max_arguments,
    message_handler);
  const std::string resolutions = options.get_option("dynamic-resolutions");
  if(!resolutions.empty())
  {
    convert_options.resolutions.clear();
    read_dynamic_resolutions(
      resolutions, message_handler, convert_options.resolutions);
  }
  const std::string strings = options.get_option("shell-strings");
  if(strings == "refined")
    convert_options.strings = shell_string_representationt::REFINED;
  else if(strings == "native")
    convert_options.strings = shell_string_representationt::NATIVE;
  else if(!strings.empty())
  {
    messaget log{message_handler};
    log.error() << "--shell-strings must be refined or native; using refined"
                << messaget::eom;
  }
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
  // The EXIT trap runs when the shell exits.
  if(symbol_table.has_symbol("shell::run_exit_trap"))
  {
    body.add(code_function_callt{
      symbol_table.lookup_ref("shell::run_exit_trap").symbol_expr()});
  }

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
  // A constant string, as in a trace: 'text', or <unset>. Only native
  // strings have constant contents.
  if(
    expr.id() == ID_struct && expr.type().id() == ID_struct_tag &&
    to_struct_tag_type(expr.type()).get_identifier() ==
      "tag-" SHELL_VALUE_TAG &&
    expr.operands().size() >= 2)
  {
    const auto set = numeric_cast<mp_integer>(expr.operands()[0]);
    if(set.has_value() && *set == 0)
      return "<unset>";
    const exprt &value = expr.operands()[1];
    if(value.is_constant() && value.type().id() == ID_string)
    {
      std::string quoted = "'";
      for(char c : id2string(to_constant_expr(value).get_value()))
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
