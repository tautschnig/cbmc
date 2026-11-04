/// \file
/// Shell converter: trap (XCU 2.15 trap).
///
/// Signals are not modelled: the script receives none, so that only the
/// EXIT trap runs. Its action is the text of the trap command's first
/// operand, which is parsed during conversion where it is known, and runs
/// when the shell or a subshell exits. The ghost shell::exit_trap holds
/// the number of the current action (0 for none); the function
/// shell::run_exit_trap runs it.

#include "shell_convert_class.h"

#include <goto-programs/goto_instruction_code.h>

#include <cctype>

/// The value of shell::exit_trap for an action not known during conversion.
static const unsigned unknown_exit_trap = 0xffff;

static typet exit_trap_type()
{
  return unsignedbv_typet{16};
}

symbol_exprt shell_convertert::exit_trap()
{
  return global_symbol("shell::exit_trap", "exit_trap", exit_trap_type());
}

symbol_exprt shell_convertert::in_exit_trap()
{
  return global_symbol("shell::in_exit_trap", "in_exit_trap", bool_typet{});
}

symbol_exprt shell_convertert::exit_trap_status()
{
  return global_symbol(
    "shell::exit_trap_status", "exit_trap_status", status_type());
}

/// Whether \p word is a condition that all shells accept: EXIT, 0, a
/// signal number, or a signal name without SIG.
static bool is_trap_condition(const std::string &word)
{
  static const std::set<std::string> names{
    "EXIT", "HUP",  "INT",   "QUIT", "ILL",   "TRAP", "ABRT", "BUS",
    "FPE",  "KILL", "USR1",  "SEGV", "USR2",  "PIPE", "ALRM", "TERM",
    "CHLD", "CONT", "STOP",  "TSTP", "TTIN",  "TTOU", "URG",  "XCPU",
    "XFSZ", "PROF", "WINCH", "SYS",  "VTALRM"};
  if(names.count(word) != 0)
    return true;
  if(word.empty() || word.size() > 2)
    return false;
  for(char c : word)
  {
    if(!std::isdigit(static_cast<unsigned char>(c)))
      return false;
  }
  return std::stoi(word) <= 64;
}

void shell_convertert::convert_trap(
  const shell_commandt &command,
  code_blockt &dest)
{
  const source_locationt &loc = command.location;
  std::size_t first = 1;
  if(first < command.words.size() && static_text(command.words[first]) == "--")
    ++first;
  if(first >= command.words.size())
  {
    // trap without operands writes the traps, which can fail.
    nondet_status(dest, 255, loc);
    return;
  }
  // The conditions must be literal text.
  std::vector<std::string> conditions;
  const auto action_text = static_text(command.words[first]);
  // An unsigned integer first: all operands are conditions to reset.
  bool reset =
    action_text.has_value() && !action_text->empty() &&
    action_text->find_first_not_of("0123456789") == std::string::npos;
  const std::size_t conditions_start = reset ? first : first + 1;
  if(conditions_start >= command.words.size())
    unsupported(loc, "trap with an action and no condition");
  for(std::size_t i = conditions_start; i < command.words.size(); ++i)
  {
    const auto condition = static_text(command.words[i]);
    if(!condition.has_value())
      unsupported(loc, "trap condition that is an expansion");
    conditions.push_back(*condition);
  }
  if(!reset && action_text == "-")
    reset = true;

  bool exit_condition = false;
  bool invalid = false;
  bool sig_prefix = false;
  for(const auto &condition : conditions)
  {
    if(condition == "EXIT" || condition == "0")
      exit_condition = true;
    else if(is_trap_condition(condition))
      continue;
    else if(
      condition.rfind("SIG", 0) == 0 && is_trap_condition(condition.substr(3)))
    {
      // dash rejects these; bash, busybox, mksh and zsh accept them.
      sig_prefix = true;
    }
    else
      invalid = true;
  }

  if(exit_condition)
  {
    if(program.dialect == shell_dialectt::ZSH && in_function)
    {
      // zsh runs the EXIT trap of a function when the function returns.
      unsupported(loc, "trap on EXIT in a zsh function");
    }
    if(exit_trap_location.get_file().empty())
      exit_trap_location = loc;
    unsigned number = 0;
    if(!reset)
    {
      const auto text = known_text(command.words[first]);
      if(!text.has_value())
      {
        expand_word(
          command.words[first], shell_word_contextt::IGNORED, dest, loc);
        number = unknown_exit_trap;
      }
      else if(!text->empty())
        number = register_exit_trap(*text, loc);
    }
    assign(dest, exit_trap(), from_integer(number, exit_trap_type()), loc);
  }
  else if(!reset)
  {
    // The action of other conditions never runs, as no signal arrives;
    // its expansions are evaluated.
    expand_word(command.words[first], shell_word_contextt::IGNORED, dest, loc);
  }

  if(invalid)
    set_status(dest, 1, loc);
  else if(sig_prefix && program.dialect == shell_dialectt::POSIX)
    nondet_status(dest, 1, loc);
  else
    set_status(dest, 0, loc);
}

unsigned shell_convertert::register_exit_trap(
  const std::string &text,
  const source_locationt &loc)
{
  for(std::size_t i = 0; i < exit_traps.size(); ++i)
  {
    if(exit_traps[i].text == text)
      return i + 1;
  }
  if(exit_traps.size() + 1 >= unknown_exit_trap)
    unsupported(loc, "too many EXIT trap actions");
  exit_trapt trap;
  trap.text = text;
  trap.identifier = "shell::exit_trap#" + std::to_string(exit_traps.size() + 1);
  trap.location = loc;
  trap.commands = &parse_known_text(text, "trap", loc);
  symbolt function{
    trap.identifier, code_typet{{}, empty_typet{}}, shell_mode()};
  function.base_name = trap.identifier;
  function.pretty_name = "EXIT trap " + text;
  function.location = loc;
  symbol_table.add(function);
  exit_traps.push_back(std::move(trap));
  return exit_traps.size();
}

void shell_convertert::convert_exit_traps()
{
  // Actions can set further traps: those are added while converting.
  for(std::size_t i = 0; i < exit_traps.size(); ++i)
  {
    const std::string identifier = exit_traps[i].identifier;
    const shell_command_listt &commands = *exit_traps[i].commands;
    const std::string saved_function = current_function;
    auto saved_loops = std::move(loops);
    loops.clear();
    current_function = identifier;
    const bool saved_in_function = in_function;
    in_function = false;
    // Nothing is known where the shell exits.
    auto saved_literals = std::move(known_literals);
    known_literals.clear();
    code_blockt body;
    convert_list(commands, body);
    known_literals = std::move(saved_literals);
    in_function = saved_in_function;
    current_function = saved_function;
    loops = std::move(saved_loops);
    symbol_table.get_writeable_ref(identifier).value = std::move(body);
  }
}

void shell_convertert::add_run_exit_trap()
{
  if(!symbol_table.has_symbol("shell::exit_trap"))
    return;
  // The location of the first trap on EXIT.
  const source_locationt loc = exit_trap_location;
  const std::string identifier = "shell::run_exit_trap";
  current_function = identifier;
  code_blockt body;
  code_blockt run;
  // The trap is cleared, so that it runs once.
  const symbol_exprt number = temporary(exit_trap_type(), run, loc);
  assign(run, number, exit_trap(), loc);
  assign(run, exit_trap(), from_integer(0, exit_trap_type()), loc);
  // The action runs with $? the exit status, which it keeps unless the
  // action exits; exit without an operand in the action exits with it
  // (XCU 2.15 exit), except in zsh.
  assign(run, exit_trap_status(), status(), loc);
  assign(run, in_exit_trap(), true_exprt{}, loc);
  assign(run, exited(), false_exprt{}, loc);
  code_blockt unknown;
  check(
    unknown,
    false_exprt{},
    "shell-unsupported",
    "the action of the EXIT trap is known during conversion (other actions "
    "are not supported)",
    loc);
  add(unknown, code_assumet{false_exprt{}}, loc);
  add(
    run,
    code_ifthenelset{
      equal_exprt{number, from_integer(unknown_exit_trap, exit_trap_type())},
      std::move(unknown)},
    loc);
  for(std::size_t i = 0; i < exit_traps.size(); ++i)
  {
    code_blockt call;
    add(
      call,
      code_function_callt{
        symbol_table.lookup_ref(exit_traps[i].identifier).symbol_expr()},
      loc);
    add(
      run,
      code_ifthenelset{
        equal_exprt{number, from_integer(i + 1, exit_trap_type())},
        std::move(call)},
      loc);
  }
  code_blockt keep;
  assign(keep, status(), exit_trap_status(), loc);
  add(run, code_ifthenelset{not_exprt{exited()}, std::move(keep)}, loc);
  assign(run, exited(), true_exprt{}, loc);
  assign(run, in_exit_trap(), false_exprt{}, loc);
  add(
    body,
    code_ifthenelset{
      notequal_exprt{exit_trap(), from_integer(0, exit_trap_type())},
      std::move(run)},
    loc);
  symbolt function{identifier, code_typet{{}, empty_typet{}}, shell_mode()};
  function.base_name = "run_exit_trap";
  function.pretty_name = "run_exit_trap";
  function.location = loc;
  function.value = std::move(body);
  symbol_table.add(function);
  current_function = SHELL_MAIN_FUNCTION;
}
