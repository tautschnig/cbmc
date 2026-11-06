/// \file
/// Shell converter: subshell environments (POSIX.1-2024 XCU 2.13).
///
/// A subshell starts as a copy of the shell's state, and its changes are
/// lost when it ends; only its exit status remains. Each subshell becomes
/// a GOTO function shell::subshell#N, whose body is in
/// shell::subshell#N::body. The function saves the whole state, calls the
/// body, and restores the state except the status. It is built after the
/// rest of the script, once the state's variables are known.
///
/// Command substitutions, the commands of pipelines, and asynchronous
/// lists run in subshells. Their outputs are not modelled, and the
/// commands of a pipeline or an asynchronous list run one after another
/// where the shell would start them: as they cannot see each other's
/// changes to the shell state, this is exact for everything the model
/// observes.

#include "shell_convert_class.h"

#include <goto-programs/goto_instruction_code.h>

void shell_convertert::run_in_subshell(
  const source_locationt &loc,
  code_blockt &dest,
  const std::function<void(code_blockt &)> &convert)
{
  // Reserve the number first: the body can contain further subshells.
  const std::string identifier =
    "shell::subshell#" + std::to_string(subshells.size() + 1);
  const std::string body_identifier = identifier + "::body";
  subshells.push_back({identifier, loc});

  const std::string saved_function = current_function;
  auto saved_loops = std::move(loops);
  loops.clear();
  current_function = body_identifier;
  // The subshell's changes to variables are lost when it ends; a return
  // in it ends the subshell.
  const auto saved_literals = known_literals;
  auto saved_locals = std::move(function_locals);
  function_locals.reset();
  const irep_idt saved_end = function_end_label;
  function_end_label.clear();
  code_blockt body;
  convert(body);
  function_end_label = saved_end;
  function_locals = std::move(saved_locals);
  known_literals = saved_literals;
  current_function = saved_function;
  loops = std::move(saved_loops);

  for(const std::string &id : {identifier, body_identifier})
  {
    symbolt function{id, code_typet{{}, empty_typet{}}, shell_mode()};
    function.base_name = id;
    function.pretty_name = "subshell";
    function.is_lvalue = true;
    function.location = loc;
    symbol_table.add(function);
  }
  symbol_table.get_writeable_ref(body_identifier).value = std::move(body);

  add(
    dest,
    code_function_callt{symbol_table.lookup_ref(identifier).symbol_expr()},
    loc);
}

void shell_convertert::build_subshell_wrappers()
{
  for(const auto &subshell : subshells)
  {
    const source_locationt &loc = subshell.location;
    current_function = subshell.identifier;
    code_blockt body;
    std::vector<std::pair<symbol_exprt, symbol_exprt>> saved;
    for(const auto &identifier : state_symbols)
    {
      if(identifier == "shell::status")
        continue;
      const symbol_exprt global =
        symbol_table.lookup_ref(identifier).symbol_expr();
      const symbol_exprt copy = temporary(global.type(), body, loc);
      assign(body, copy, global, loc);
      saved.emplace_back(global, copy);
    }
    // Traps are reset in subshells (XCU 2.13), and the subshell's EXIT
    // trap runs when it exits.
    const bool traps = symbol_table.has_symbol("shell::run_exit_trap");
    if(traps)
      assign(body, exit_trap(), from_integer(0, exit_trap().type()), loc);
    add(
      body,
      code_function_callt{
        symbol_table.lookup_ref(subshell.identifier + "::body").symbol_expr()},
      loc);
    if(traps)
    {
      add(
        body,
        code_function_callt{
          symbol_table.lookup_ref("shell::run_exit_trap").symbol_expr()},
        loc);
    }
    // exit in the subshell only ends the subshell.
    for(const auto &pair : saved)
      assign(body, pair.first, pair.second, loc);
    symbol_table.get_writeable_ref(subshell.identifier).value = std::move(body);
  }
  current_function = SHELL_MAIN_FUNCTION;
}

void shell_convertert::convert_subshell(
  const shell_commandt &command,
  code_blockt &dest)
{
  run_in_subshell(
    command.location,
    dest,
    [&](code_blockt &body) { convert_list(command.body, body); });
}

exprt shell_convertert::expand_command_substitution(
  const shell_word_partt &part,
  code_blockt &dest)
{
  const source_locationt &loc = part.location;
  if(part.process_substitution)
  {
    // <(list) and >(list) run the list asynchronously, as list & does,
    // and expand to the name of a file connected to it (/dev/fd/N in
    // bash and zsh on Linux).
    run_in_subshell(
      loc,
      dest,
      [&](code_blockt &body)
      {
        if(part.commands != nullptr)
          convert_list(*part.commands, body);
      });
    const symbol_exprt fd = temporary(integer_type(), dest, loc);
    assign(dest, fd, nondet(integer_type(), loc), loc);
    add(
      dest,
      code_assumet{and_exprt{
        binary_relation_exprt{fd, ID_ge, from_integer(3, integer_type())},
        binary_relation_exprt{fd, ID_le, from_integer(65535, integer_type())}}},
      loc);
    return concat(
      string_literal("/dev/fd/", loc),
      from_integer_expr(fd, dest, loc),
      dest,
      loc);
  }
  // dash, mksh, busybox, and yash keep $? until the command completes;
  // bash and zsh set it to the status of the substitution at once.
  const bool keep_status = program.dialect == shell_dialectt::POSIX ||
                           program.dialect == shell_dialectt::MKSH;
  const symbol_exprt previous = temporary(status_type(), dest, loc);
  assign(dest, previous, status(), loc);
  // bash, outside POSIX mode, turns -e off in command substitutions.
  const bool errexit_off = program.dialect == shell_dialectt::BASH;
  const bool saved_errexit_ignored = errexit_ignored;
  run_in_subshell(
    loc,
    dest,
    [&](code_blockt &body)
    {
      if(errexit_off)
      {
        errexit_ignored = true;
        if(option_possible("errexit"))
          assign(body, errexit_ignored_in_caller(), true_exprt{}, loc);
      }
      if(part.commands != nullptr)
        convert_list(*part.commands, body);
      else
        set_status(body, 0, loc);
    });
  errexit_ignored = saved_errexit_ignored;
  assign(dest, substitution_status(), status(), loc);
  if(keep_status)
    assign(dest, status(), previous, loc);
  assign(dest, substitution_ran(), true_exprt{}, loc);
  // The output is modelled for specified utilities only.
  if(const auto output = utility_output(part, dest))
    return *output;
  return nondet_string(dest, loc);
}

/// The commands of a pipeline run in subshells (XCU 2.9.2), except that
/// zsh runs the last one in the current shell. The status is that of the
/// last command.
void shell_convertert::convert_pipeline(
  const shell_commandt &command,
  code_blockt &dest)
{
  const source_locationt &loc = command.location;
  const bool pipefail = option_possible("pipefail");
  // With pipefail, the status is that of the last command that failed.
  const symbol_exprt failed = temporary(status_type(), dest, loc);
  if(pipefail)
    assign(dest, failed, from_integer(0, status_type()), loc);
  const std::size_t last = command.body.size() - 1;
  for(std::size_t i = 0; i < command.body.size(); ++i)
  {
    const shell_commandt &element = command.body[i];
    if(i == last && program.dialect == shell_dialectt::ZSH)
      convert_command(element, dest);
    else
    {
      run_in_subshell(
        element.location,
        dest,
        [&](code_blockt &body) { convert_command(element, body); });
    }
    if(pipefail)
    {
      assign(
        dest,
        failed,
        if_exprt{
          equal_exprt{status(), from_integer(0, status_type())},
          failed,
          status()},
        loc);
    }
  }
  if(pipefail)
  {
    assign(dest, status(), if_exprt{option("pipefail"), failed, status()}, loc);
  }
}

/// An asynchronous list runs in a subshell; its status is zero
/// (XCU 2.9.3.1).
void shell_convertert::convert_background(
  const shell_commandt &command,
  code_blockt &dest)
{
  if(command.kind == shell_commandt::kindt::FUNCTION_DEFINITION)
    unsupported(command.location, "function definition with &");
  run_in_subshell(
    command.location,
    dest,
    [&](code_blockt &body) { convert_foreground(command, body); });
  set_status(dest, 0, command.location);
  // $! is the process ID of the asynchronous list.
  {
    const source_locationt &loc = command.location;
    const symbol_exprt pid = temporary(integer_type(), dest, loc);
    assign(dest, pid, nondet(integer_type(), loc), loc);
    add(
      dest,
      code_assumet{and_exprt{
        binary_relation_exprt{pid, ID_ge, from_integer(1, integer_type())},
        binary_relation_exprt{
          pid, ID_le, from_integer(4194304, integer_type())}}},
      loc);
    assign(dest, special("!"), from_integer_expr(pid, dest, loc), loc);
  }
}
