/// \file
/// Conversion of the shell IR into symbols and code for CBMC.
///
/// Every shell value is a struct of a string and its set flag (see
/// shell_convert_strings.cpp). The script becomes the function shell::main; shell variables are static-lifetime
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
  if(symbol_table.has_symbol("shell::ifs_default"))
    assign(init, ifs_default(), true_exprt{}, loc);
  if(symbol_table.has_symbol("shell::ifs_unset"))
    assign(init, ifs_unset(), false_exprt{}, loc);
  if(symbol_table.has_symbol("shell::exit_trap"))
    assign(init, exit_trap(), from_integer(0, exit_trap().type()), loc);
  if(symbol_table.has_symbol("shell::in_exit_trap"))
    assign(init, in_exit_trap(), false_exprt{}, loc);
  if(posix_increments_used)
    assign(init, posix_increments(), nondet(bool_typet{}, loc), loc);
  initialize_options(init, loc);
  initialize_getopts(init, loc);
  initialize_arrays(init, loc);
  if(option_possible("errexit"))
    assign(init, errexit_ignored_in_caller(), false_exprt{}, loc);
  assign(init, substitution_status(), from_integer(0, status_type()), loc);
  assign(init, substitution_ran(), false_exprt{}, loc);
  // No functions are defined at the start: functions exported by bash in
  // the environment are not modelled (see the plan's assumptions).
  for(const auto &entry : function_definitions)
  {
    assign(
      init,
      current_definition(entry.first),
      from_integer(0, current_definition_type()),
      loc);
  }
  // The positional parameters are unknown strings, which cost the
  // solvers even when unused: initialise them only if the script uses
  // them.
  if(
    symbol_table.has_symbol("shell::args") ||
    symbol_table.has_symbol("shell::argc"))
  {
    initialize_positional_parameters(init, loc);
  }

  // Variables not yet assigned are inherited from the environment.
  for(const auto &name : variables)
  {
    const symbol_exprt var =
      is_bash_readonly_id(name) ? bash_readonly_id(name) : variable(name, loc);
    // Every shell sets OPTIND to 1 at startup.
    if(name == "OPTIND")
      assign(init, var, string_literal("1", loc), loc);
    else if(
      is_bash_readonly_id(name) && program.dialect == shell_dialectt::BASH)
    {
      // A user or process ID.
      const symbol_exprt id = temporary(integer_type(), init, loc);
      assign(init, id, nondet(integer_type(), loc), loc);
      add(
        init,
        code_assumet{and_exprt{
          binary_relation_exprt{id, ID_ge, from_integer(0, integer_type())},
          binary_relation_exprt{
            id, ID_le, from_integer(4294967295LL, integer_type())}}},
        loc);
      assign(init, var, from_integer_expr(id, init, loc), loc);
    }
    else
      assign(init, var, environment_string(init, loc), loc);
  }
  // $0, $$, and $- are set; their values depend on how the shell was run.
  for(const auto &name : special_parameters)
  {
    const symbol_exprt param = special(name);
    // Before any asynchronous list, $! is unset, and 0 in zsh.
    if(name == "!")
    {
      assign(
        init,
        param,
        program.dialect == shell_dialectt::ZSH ? string_literal("0", loc)
                                               : unset_string(loc),
        loc);
    }
    else
      assign(init, param, nondet_string(init, loc), loc);
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
  add_value_type();
  collect_dot_candidates(program.commands);
  collect_dot_scripts(program.commands);
  collect_function_definitions(program.commands);
  collect_options(program.commands);
  collect_arrays(program.commands);
  collect_variable_names(program.commands);
  for(const auto &dot : dot_scripts)
  {
    if(dot.second != nullptr)
    {
      collect_variable_names(dot.second->commands);
      collect_function_definitions(dot.second->commands);
      collect_options(dot.second->commands);
      collect_arrays(dot.second->commands);
    }
  }
  nounset_possible = option_possible("nounset");
  declare_functions();
  declare_dot_scripts();
  code_blockt body;
  // Every shell starts with the default IFS.
  known_literals["IFS"] = initial_ifs();
  convert_list(program.commands, body);
  add_main(std::move(body));
  convert_function_bodies();
  convert_dot_scripts();
  convert_exit_traps();
  add_run_exit_trap();
  build_subshell_wrappers();
  add_initialize();
}

bool shell_convert(
  const shell_programt &program,
  const shell_convert_optionst &options,
  symbol_table_baset &symbol_table,
  message_handlert &message_handler)
{
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
