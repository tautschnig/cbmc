/// \file
/// Conversion of the shell IR into symbols and code for CBMC.
///
/// Every shell value is a bounded string (see shell_runtime.h). The script
/// becomes the function shell::main; shell variables are static-lifetime
/// symbols initialised from a nondeterministic environment. The exit
/// status of the last command is the symbol shell::status.
///
/// Throughout, "unsupported" constructs are reported as errors: they are
/// never dropped, so verification results are not claimed for scripts
/// whose behaviour was not modelled.

#include "shell_convert_class.h"

#include <util/cprover_prefix.h>
#include <util/std_code.h>
#include <util/symbol_table_base.h>

#include "shell_converter.h"
#include "shell_runtime.h"

const irep_idt &shell_mode()
{
  static const irep_idt mode{"shell"};
  return mode;
}

// ------------------------------------------------------------------
// Initial state

void shell_convertert::add_main(code_blockt body)
{
  symbolt main{
    SHELL_MAIN_FUNCTION, code_typet{{}, empty_typet{}}, shell_mode()};
  main.base_name = "main";
  main.pretty_name = program.filename;
  main.is_lvalue = true;
  main.location.set_file(program.filename);
  main.value = std::move(body);
  symbol_table.add(main);
}

/// __CPROVER_initialize: the state in which the script starts.
void shell_convertert::add_initialize()
{
  source_locationt loc;
  loc.set_file(program.filename);
  code_blockt init;
  init.add(code_labelt{CPROVER_PREFIX "HIDE", code_skipt{}});

  set_status(init, 0, loc);
  assign(init, exited(), false_exprt{}, loc);
  initialize_positional_parameters(init, loc);

  // Variables not yet assigned are inherited from the environment.
  for(const auto &name : variables)
  {
    const symbol_exprt var = variable(name);
    assign(init, var, call(SHELL_RT_ENVIRONMENT, {}, init, loc), loc);
  }
  // $0, $$, and $- are set; their values depend on how the shell was run.
  for(const auto &name : special_parameters)
  {
    const symbol_exprt param = special(name);
    assign(init, param, call(SHELL_RT_NONDET_STRING, {}, init, loc), loc);
  }

  const std::string init_name = CPROVER_PREFIX "initialize";
  if(symbol_table.has_symbol(init_name))
    symbol_table.remove(init_name);
  symbolt init_symbol{init_name, code_typet{{}, empty_typet{}}, shell_mode()};
  init_symbol.base_name = init_name;
  init_symbol.pretty_name = init_name;
  init_symbol.is_lvalue = true;
  init_symbol.value = std::move(init);
  symbol_table.add(init_symbol);
}

void shell_convertert::operator()()
{
  code_blockt body;
  convert_list(program.commands, body);
  add_main(std::move(body));
  add_initialize();
}

bool shell_convert(
  const shell_programt &program,
  const shell_convert_optionst &options,
  symbol_table_baset &symbol_table,
  message_handlert &message_handler)
{
  if(add_shell_runtime(
       options.max_string_length, symbol_table, message_handler))
  {
    return true;
  }
  try
  {
    shell_convertert{program, options, symbol_table, message_handler}();
  }
  catch(const shell_conversion_errort &)
  {
    return true;
  }
  return false;
}
