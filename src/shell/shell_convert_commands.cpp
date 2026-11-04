/// \file
/// Shell converter: commands (POSIX.1-2024 XCU 2.9).

#include "shell_convert_class.h"

#include <goto-programs/goto_instruction_code.h>

#include <algorithm>

// ------------------------------------------------------------------
// Commands

/// The builtins that the converter models, by name.
static const std::map<std::string, shell_command_kindt> builtins{
  {":", shell_command_kindt::COLON},
  {"true", shell_command_kindt::TRUE},
  {"false", shell_command_kindt::FALSE},
  {"test", shell_command_kindt::TEST},
  {"[", shell_command_kindt::BRACKET},
  {"exit", shell_command_kindt::EXIT},
  {"export", shell_command_kindt::EXPORT},
  {"unset", shell_command_kindt::UNSET},
  {"break", shell_command_kindt::BREAK},
  {"continue", shell_command_kindt::CONTINUE},
  {"shift", shell_command_kindt::SHIFT},
  {"set", shell_command_kindt::SET},
  {"return", shell_command_kindt::RETURN},
  {"exec", shell_command_kindt::EXEC},
  {".", shell_command_kindt::DOT},
  {CPROVER_PREFIX "assert", shell_command_kindt::ASSERT},
  {CPROVER_PREFIX "assume", shell_command_kindt::ASSUME},
  {"echo", shell_command_kindt::OUTPUT},
  {"printf", shell_command_kindt::OUTPUT}};

/// Builtins that change the shell environment (XCU 2.15 special
/// builtins, regular builtins of XCU 1.7, and common extensions).
static const std::set<std::string> unsupported_builtins{
  "eval",      "readonly", "times",     "trap",     "cd",       "read",
  "getopts",   "alias",    "unalias",   "umask",    "wait",     "jobs",
  "fg",        "bg",       "command",   "type",     "hash",     "kill",
  "ulimit",    "fc",       "newgrp",    "local",    "declare",  "typeset",
  "source",    "let",      "shopt",     "builtin",  "enable",   "mapfile",
  "readarray", "pushd",    "popd",      "dirs",     "disown",   "suspend",
  "logout",    "nameref",  "autoload",  "setopt",   "unsetopt", "emulate",
  "print",     "whence",   "functions", "zmodload", "caller",   "compgen",
  "complete"};

shell_command_kindt shell_convertert::classify(const std::string &name) const
{
  const auto it = builtins.find(name);
  if(it != builtins.end())
    return it->second;
  if(name == "source" && program.dialect != shell_dialectt::POSIX)
    return shell_command_kindt::DOT;
  if(unsupported_builtins.count(name) != 0)
    return shell_command_kindt::UNSUPPORTED_BUILTIN;
  if(name.rfind(CPROVER_PREFIX, 0) == 0)
    return shell_command_kindt::UNSUPPORTED_BUILTIN;
  return shell_command_kindt::EXTERNAL;
}

/// Reject variables that shells maintain specially, whose values and
/// assignments would need modelling.
void shell_convertert::check_parameter_name(
  const std::string &name,
  const source_locationt &loc)
{
  static const std::set<std::string> special_everywhere{
    "LINENO", "RANDOM", "SECONDS", "OPTIND", "OPTARG", "IFS", "_"};
  static const std::set<std::string> special_bash{
    "BASH_ARGC",    "BASH_ARGV",     "BASH_COMMAND", "BASH_LINENO",
    "BASH_SOURCE",  "BASH_SUBSHELL", "BASHPID",      "EPOCHREALTIME",
    "EPOCHSECONDS", "FUNCNAME",      "GROUPS",       "HISTCMD",
    "PIPESTATUS",   "SRANDOM",       "UID",          "EUID",
    "PPID",         "SHELLOPTS",     "BASHOPTS",     "BASH_REMATCH",
    "DIRSTACK",     "REPLY"};
  static const std::set<std::string> special_zsh{
    "status",
    "argv",
    "pipestatus",
    "ERRNO",
    "TTYIDLE",
    "path",
    "PATH",
    "fpath",
    "cdpath",
    "manpath",
    "REPLY",
    "reply",
    "PPID",
    "EPOCHSECONDS",
    "EPOCHREALTIME"};
  static const std::set<std::string> special_mksh{
    "KSH_VERSION",
    "BASHPID",
    "EPOCHREALTIME",
    "PPID",
    "PIPESTATUS",
    "USER_ID",
    "REPLY"};
  bool special = special_everywhere.count(name) != 0;
  if(program.dialect == shell_dialectt::BASH)
    special |= special_bash.count(name) != 0;
  else if(program.dialect == shell_dialectt::ZSH)
    special |= special_zsh.count(name) != 0;
  else if(program.dialect == shell_dialectt::MKSH)
    special |= special_mksh.count(name) != 0;
  if(special)
    unsupported(loc, "variable " + name + ", which the shell maintains");
}

void shell_convertert::assign_variable(
  const std::string &name,
  const exprt &value,
  code_blockt &dest,
  const source_locationt &loc)
{
  // Expansion results are always set.
  assign(dest, variable(name, loc), value, loc);
  note_assignment(name, value);
}

code_ifthenelset shell_convertert::if_status_zero(bool zero, code_blockt block)
{
  const exprt is_zero = equal_exprt{status(), from_integer(0, status_type())};
  return code_ifthenelset{
    zero ? is_zero : exprt{not_exprt{is_zero}}, std::move(block)};
}

void shell_convertert::goto_label(code_blockt &dest, const irep_idt &label)
{
  dest.add(code_gotot{label});
}

void shell_convertert::label(code_blockt &dest, const irep_idt &label)
{
  dest.add(code_labelt{label, code_skipt{}});
}

/// The condition of if, while, or until, where -e is ignored.
void shell_convertert::convert_condition(
  const shell_command_listt &list,
  code_blockt &dest)
{
  const bool saved_errexit_ignored = errexit_ignored;
  errexit_ignored = true;
  convert_list(list, dest);
  errexit_ignored = saved_errexit_ignored;
}

void shell_convertert::convert_list(
  const shell_command_listt &list,
  code_blockt &dest)
{
  for(const auto &command : list)
    convert_command(command, dest);
}

void shell_convertert::convert_command(
  const shell_commandt &command,
  code_blockt &dest)
{
  const std::size_t live = live_temporaries.size();
  if(command.kind != shell_commandt::kindt::SIMPLE)
    forget_assigned(command);
  if(command.background)
    convert_background(command, dest);
  else
    convert_foreground(command, dest);
  if(command.kind != shell_commandt::kindt::SIMPLE)
    forget_assigned(command);
  // Temporaries hold values within one command. Ending them there keeps
  // goto-symex from merging their values at the join points of later
  // control flow (loops are labels and gotos in one block), which made
  // the string solvers reason about if-expressions over strings.
  for(std::size_t i = live; i < live_temporaries.size(); ++i)
  {
    code_deadt dead{live_temporaries[i]};
    dead.add_source_location() = command.location;
    dest.add(std::move(dead));
  }
  live_temporaries.erase(
    live_temporaries.begin() + live, live_temporaries.end());
}

void shell_convertert::convert_foreground(
  const shell_commandt &command,
  code_blockt &dest)
{
  using kindt = shell_commandt::kindt;
  if(!command.redirects.empty() && command.kind != kindt::SIMPLE)
    unsupported(command.location, "redirection of a compound command");

  // -e is ignored in a pipeline beginning with ! (XCU 2.15 set).
  const bool saved_errexit_ignored = errexit_ignored;
  if(command.negated)
    errexit_ignored = true;

  switch(command.kind)
  {
  case kindt::SIMPLE:
    convert_simple(command, dest);
    break;
  case kindt::PIPELINE:
    convert_pipeline(command, dest);
    break;
  case kindt::AND:
  case kindt::OR:
    convert_and_or(command, dest);
    break;
  case kindt::SUBSHELL:
  case kindt::BRACE_GROUP:
  case kindt::IF:
  case kindt::WHILE:
  case kindt::UNTIL:
  case kindt::FOR:
  case kindt::CASE:
  case kindt::FUNCTION_DEFINITION:
    convert_compound(command, dest);
    break;
  }
  errexit_ignored = saved_errexit_ignored;

  // A failure of a compound command other than a subshell comes from a
  // command inside it, which was checked there (XCU 2.15 set).
  if(
    !command.negated &&
    (command.kind == kindt::SIMPLE || command.kind == kindt::PIPELINE ||
     command.kind == kindt::SUBSHELL))
  {
    check_errexit(dest, command.location);
  }
  if(command.negated)
  {
    // ! pipeline: 0 becomes 1, any other status becomes 0 (XCU 2.9.2).
    assign(
      dest,
      status(),
      if_exprt{
        equal_exprt{status(), from_integer(0, status_type())},
        from_integer(1, status_type()),
        from_integer(0, status_type())},
      command.location);
  }
}

void shell_convertert::convert_compound(
  const shell_commandt &command,
  code_blockt &dest)
{
  using kindt = shell_commandt::kindt;
  switch(command.kind)
  {
  case kindt::BRACE_GROUP:
    convert_list(command.body, dest);
    return;
  case kindt::IF:
    convert_if(command, dest);
    return;
  case kindt::WHILE:
  case kindt::UNTIL:
    convert_while(command, dest);
    return;
  case kindt::FOR:
    convert_for(command, dest);
    return;
  case kindt::CASE:
    convert_case(command, dest);
    return;
  case kindt::SUBSHELL:
    convert_subshell(command, dest);
    return;
  case kindt::FUNCTION_DEFINITION:
    convert_function_definition(command, dest);
    return;
  case kindt::SIMPLE:
  case kindt::PIPELINE:
  case kindt::AND:
  case kindt::OR:
    UNREACHABLE;
  }
}

void shell_convertert::convert_and_or(
  const shell_commandt &command,
  code_blockt &dest)
{
  // -e is ignored for all commands of an AND-OR list but the last.
  const bool saved_errexit_ignored = errexit_ignored;
  errexit_ignored = true;
  convert_command(command.body[0], dest);
  errexit_ignored = saved_errexit_ignored;
  code_blockt rhs;
  convert_command(command.body[1], rhs);
  add(
    dest,
    if_status_zero(command.kind == shell_commandt::kindt::AND, std::move(rhs)),
    command.location);
}

void shell_convertert::convert_if(
  const shell_commandt &command,
  code_blockt &dest)
{
  const source_locationt &loc = command.location;
  convert_condition(command.condition, dest);
  // Both branches start from the state after the condition.
  const auto after_condition = known_literals;
  code_blockt then_block;
  convert_list(command.body, then_block);
  known_literals = after_condition;
  code_blockt else_block;
  if(command.else_body.empty())
  {
    // With no branch executed, the status is zero (XCU 2.9.4.4).
    set_status(else_block, 0, loc);
  }
  else
    convert_list(command.else_body, else_block);
  add(
    dest,
    code_ifthenelset{
      equal_exprt{status(), from_integer(0, status_type())},
      std::move(then_block),
      std::move(else_block)},
    loc);
}

/// Convert \p body into a block of its own in \p dest. Jumps out of it, such
/// as break and continue, then skip no declarations of temporaries in the
/// scope of their target, which goto_convert would otherwise implement with
/// __CPROVER_going_to flags, and symbolic execution with merges whose values
/// the string solver would traverse as trees.
void shell_convertert::convert_body(
  const shell_command_listt &body,
  code_blockt &dest,
  const source_locationt &loc)
{
  code_blockt block;
  convert_list(body, block);
  add(dest, std::move(block), loc);
}

/// while and until loops (XCU 2.9.4.5, 2.9.4.6) as gotos, so that break
/// and continue with a count can leave several loops.
void shell_convertert::convert_while(
  const shell_commandt &command,
  code_blockt &dest)
{
  const source_locationt &loc = command.location;
  const bool is_until = command.kind == shell_commandt::kindt::UNTIL;
  const irep_idt break_label = new_label("break");
  const irep_idt continue_label = new_label("continue");
  const symbol_exprt loop_status = temporary(status_type(), dest, loc);
  assign(dest, loop_status, from_integer(0, status_type()), loc);

  label(dest, continue_label);
  convert_condition(command.condition, dest);
  code_blockt leave;
  // The status is that of the last body executed, or zero.
  assign(leave, status(), loop_status, loc);
  goto_label(leave, break_label);
  add(dest, if_status_zero(is_until, std::move(leave)), loc);

  loops.push_back({break_label, continue_label, loop_status});
  convert_body(command.body, dest, loc);
  loops.pop_back();
  assign(dest, loop_status, status(), loc);
  goto_label(dest, continue_label);
  label(dest, break_label);
}

void shell_convertert::convert_for(
  const shell_commandt &command,
  code_blockt &dest)
{
  const source_locationt &loc = command.location;
  check_parameter_name(command.name, loc);

  // "for name; do" is "for name in "$@"; do" (XCU 2.9.4.2).
  bool dynamic = !command.has_in;
  for(const auto &word : command.for_items)
    dynamic |= contains_at(word) || is_splittable(word);
  if(dynamic)
  {
    field_listt items = [&]()
    {
      if(command.has_in)
      {
        return expand_fields(
          command.for_items.begin(), command.for_items.end(), dest, loc);
      }
      // The positional parameters always fit.
      const field_listt list = new_field_list(dest, loc);
      append_positional_parameters(list, dest, loc);
      return list;
    }();
    convert_for_loop(command, items, dest);
    return;
  }

  // The words are expanded once, before the first iteration
  // (XCU 2.9.4.2); each yields one field, so the iterations are known.
  std::vector<exprt> items;
  for(const auto &word : command.for_items)
  {
    const symbol_exprt item = temporary(string_type(), dest, loc);
    assign(
      dest,
      item,
      expand_word(word, shell_word_contextt::FIELD, dest, loc),
      loc);
    items.push_back(item);
  }

  const irep_idt break_label = new_label("break");
  const symbol_exprt loop_status = temporary(status_type(), dest, loc);
  assign(dest, loop_status, from_integer(0, status_type()), loc);
  for(const auto &item : items)
  {
    const irep_idt continue_label = new_label("continue");
    // continue reaches the next iteration from anywhere in the body.
    forget_assigned(command);
    code_blockt iteration;
    assign_variable(command.name, item, iteration, loc);
    loops.push_back({break_label, continue_label, loop_status});
    convert_body(command.body, iteration, loc);
    loops.pop_back();
    add(dest, std::move(iteration), loc);
    assign(dest, loop_status, status(), loc);
    label(dest, continue_label);
  }
  assign(dest, status(), loop_status, loc);
  label(dest, break_label);
}

/// A for loop over a list whose length is only known at run time. The
/// index is also compared with the static bound of the list, so that
/// symbolic execution stops after max_arguments iterations without an
/// unwinding bound.
void shell_convertert::convert_for_loop(
  const shell_commandt &command,
  const field_listt &items,
  code_blockt &dest)
{
  const source_locationt &loc = command.location;
  const irep_idt break_label = new_label("break");
  const irep_idt continue_label = new_label("continue");
  const irep_idt head_label = new_label("for");
  const symbol_exprt index = temporary(count_type(), dest, loc);
  const symbol_exprt loop_status = temporary(status_type(), dest, loc);
  assign(dest, index, from_integer(0, count_type()), loc);
  assign(dest, loop_status, from_integer(0, status_type()), loc);

  label(dest, head_label);
  code_blockt leave;
  goto_label(leave, break_label);
  add(
    dest,
    code_ifthenelset{
      or_exprt{
        binary_relation_exprt{
          index, ID_ge, from_integer(max_arguments, count_type())},
        binary_relation_exprt{index, ID_ge, items.count}},
      std::move(leave)},
    loc);
  code_blockt iteration;
  assign_variable(
    command.name,
    index_exprt{
      items.array, typecast_exprt::conditional_cast(index, index_type())},
    iteration,
    loc);
  loops.push_back({break_label, continue_label, loop_status});
  convert_body(command.body, iteration, loc);
  loops.pop_back();
  add(dest, std::move(iteration), loc);
  assign(dest, loop_status, status(), loc);
  label(dest, continue_label);
  assign(dest, index, plus_exprt{index, from_integer(1, count_type())}, loc);
  goto_label(dest, head_label);
  label(dest, break_label);
  assign(dest, status(), loop_status, loc);
}

void shell_convertert::convert_break_continue(
  const shell_commandt &command,
  bool is_break,
  code_blockt &dest)
{
  const source_locationt &loc = command.location;
  std::size_t n = 1;
  if(command.words.size() > 2)
    unsupported(loc, "break or continue with several operands");
  if(command.words.size() == 2)
  {
    auto text = static_text(command.words[1]);
    if(
      !text.has_value() || text->empty() ||
      text->find_first_not_of("0123456789") != std::string::npos ||
      std::stoul(*text) == 0)
    {
      unsupported(
        loc,
        "break or continue operand other than a positive "
        "decimal number");
    }
    n = std::stoul(*text);
  }
  if(loops.empty())
    unsupported(loc, "break or continue outside a loop");
  // A count larger than the number of loops leaves the outermost one.
  const shell_loop_labelst &target =
    loops[loops.size() - std::min(n, loops.size())];
  set_status(dest, 0, loc);
  if(is_break)
    goto_label(dest, target.break_label);
  else
  {
    assign(dest, target.loop_status, status(), loc);
    goto_label(dest, target.continue_label);
  }
}

/// case (XCU 2.9.4.3): the patterns are tried in order, and ";&" falls
/// through to the next item's commands.
void shell_convertert::convert_case(
  const shell_commandt &command,
  code_blockt &dest)
{
  const source_locationt &loc = command.location;
  const symbol_exprt subject = temporary(string_type(), dest, loc);
  assign(
    dest,
    subject,
    expand_word(
      command.case_word, shell_word_contextt::CASE_SUBJECT, dest, loc),
    loc);

  // The matching code and each body are blocks of their own, so that the
  // jumps between them skip no declarations (see convert_body).
  const irep_idt end_label = new_label("esac");
  std::vector<irep_idt> body_labels;
  code_blockt matching;
  for(const auto &item : command.case_items)
  {
    body_labels.push_back(new_label("case_body"));
    for(const auto &pattern : item.patterns)
    {
      const exprt matches =
        case_pattern_matches(pattern, subject, matching, item.location);
      code_blockt jump;
      goto_label(jump, body_labels.back());
      add(matching, code_ifthenelset{matches, std::move(jump)}, item.location);
    }
  }
  // No pattern matched: the status is zero.
  set_status(matching, 0, loc);
  goto_label(matching, end_label);
  add(dest, std::move(matching), loc);

  // Each item starts from the state after the matching, as items can be
  // reached by jumps and fall-through.
  const auto after_matching = known_literals;
  for(std::size_t i = 0; i < command.case_items.size(); ++i)
  {
    const auto &item = command.case_items[i];
    known_literals = after_matching;
    label(dest, body_labels[i]);
    code_blockt body;
    set_status(body, 0, item.location);
    convert_list(item.body, body);
    if(item.terminator == ";;" || i + 1 == command.case_items.size())
      goto_label(body, end_label);
    add(dest, std::move(body), item.location);
  }
  label(dest, end_label);
}

/// Whether \p pattern matches \p subject (XCU 2.14).
exprt shell_convertert::case_pattern_matches(
  const shell_wordt &pattern,
  const exprt &subject,
  code_blockt &dest,
  const source_locationt &loc)
{
  return pattern_matches(pattern, subject, dest, loc);
}

/// Redirections that cannot fail or change the shell's state: duplicating
/// the standard descriptors and /dev/null, assuming that descriptors 0,
/// 1, and 2 are open and that /dev/null exists.
static bool is_harmless_redirection(const shell_redirectt &redirect)
{
  const auto target = [&]() -> std::optional<std::string>
  {
    std::string text;
    for(const auto &part : redirect.target)
    {
      if(
        part.kind != shell_word_partt::kindt::LITERAL &&
        part.kind != shell_word_partt::kindt::QUOTED_LITERAL)
      {
        return {};
      }
      text += part.text;
    }
    return text;
  }();
  if(!target.has_value())
    return false;
  if(redirect.op == ">&" || redirect.op == "<&")
    return *target == "0" || *target == "1" || *target == "2" || *target == "-";
  return (redirect.op == ">" || redirect.op == ">>" || redirect.op == "<" ||
          redirect.op == ">|") &&
         *target == "/dev/null";
}

void shell_convertert::check_redirections(
  const shell_commandt &command,
  shell_command_kindt kind)
{
  for(const auto &redirect : command.redirects)
  {
    if(
      has_side_effects(redirect.target) ||
      has_side_effects(redirect.here_document))
    {
      // Shells differ in whether these expansions happen in the shell or
      // in the child process.
      unsupported(
        redirect.location, "redirection with an expansion with side effects");
    }
  }
  (void)kind;
}

bool shell_convertert::redirection_may_fail(const shell_commandt &command)
{
  for(const auto &redirect : command.redirects)
  {
    if(!is_harmless_redirection(redirect))
      return true;
  }
  return false;
}

void shell_convertert::with_redirections(
  const shell_commandt &command,
  bool special,
  code_blockt &dest,
  const std::function<void(code_blockt &)> &run)
{
  const source_locationt &loc = command.location;
  if(!redirection_may_fail(command))
  {
    run(dest);
    return;
  }
  // The redirections are made after the words are expanded (XCU 2.9.1.1);
  // with no side effects in them, the expansion can be skipped when the
  // redirection fails.
  for(const auto &word : command.words)
  {
    if(has_side_effects(word))
    {
      unsupported(
        loc,
        "expansion with side effects in a command whose redirection can "
        "fail");
    }
  }
  // A failed redirection skips the command with a non-zero status, and
  // makes the shell exit for special builtins (XCU 2.8.1), except in bash
  // (outside POSIX mode) and zsh.
  code_blockt failure;
  if(
    special && (program.dialect == shell_dialectt::POSIX ||
                program.dialect == shell_dialectt::MKSH))
  {
    exit_shell(failure, true, loc);
  }
  else
  {
    nondet_status(failure, 255, loc);
    add(
      failure,
      code_assumet{notequal_exprt{status(), from_integer(0, status_type())}},
      loc);
  }
  code_blockt success;
  run(success);
  add(
    dest,
    code_ifthenelset{
      nondet(bool_typet{}, loc), std::move(failure), std::move(success)},
    loc);
}

void shell_convertert::convert_assignments_only(
  const shell_commandt &command,
  code_blockt &dest)
{
  const source_locationt &loc = command.location;
  if(!command.redirects.empty())
    unsupported(loc, "redirection without a command name");
  assign(dest, substitution_ran(), false_exprt{}, loc);
  for(const auto &assignment : command.assignments)
  {
    const exprt value = expand_word(
      assignment.value,
      shell_word_contextt::ASSIGNMENT,
      dest,
      assignment.location);
    assign_variable(assignment.name, value, dest, assignment.location);
  }
  // The status is that of the last command substitution, or zero
  // (XCU 2.9.1.3).
  assign(
    dest,
    status(),
    if_exprt{
      substitution_ran(),
      substitution_status(),
      from_integer(0, status_type())},
    loc);
}

void shell_convertert::convert_simple(
  const shell_commandt &command,
  code_blockt &dest)
{
  const source_locationt &loc = command.location;
  if(command.words.empty())
  {
    convert_assignments_only(command, dest);
    return;
  }
  auto name = static_text(command.words.front());
  if(!name.has_value())
  {
    convert_dynamic_simple(command, dest);
    return;
  }
  const shell_command_kindt kind = classify(*name);
  if(*name == "eval" && function_definitions.count(*name) == 0)
  {
    // A special builtin; a redirection that can fail would make the shell
    // exit.
    check_redirections(command, kind);
    if(redirection_may_fail(command))
      unsupported(loc, "eval with a redirection that can fail");
    convert_eval(command, dest);
    return;
  }
  if(*name == "command" && function_definitions.count(*name) == 0)
  {
    convert_command_builtin(command, dest);
    return;
  }
  if(function_definitions.count(*name) == 0)
  {
    convert_non_function(command, kind, dest);
    return;
  }

  // Functions are found before regular builtins and utilities
  // (XCU 2.9.1.4), but only once their definition has run.
  check_redirections(command, kind);
  code_blockt undefined, defined;
  convert_non_function(command, kind, undefined);
  with_redirections(
    command,
    false,
    defined,
    [&](code_blockt &block) { convert_function_call(command, block); });
  add(
    dest,
    code_ifthenelset{
      equal_exprt{
        current_definition(*name), from_integer(0, current_definition_type())},
      std::move(undefined),
      std::move(defined)},
    loc);
}

bool shell_convertert::try_convert(
  const std::function<void(code_blockt &)> &convert,
  code_blockt &dest)
{
  const std::size_t live = live_temporaries.size();
  const auto saved_loops = loops;
  const bool saved_errexit_ignored = errexit_ignored;
  const bool saved_in_function = in_function;
  const std::string saved_function = current_function;
  const auto saved_literals = known_literals;
  std::set<std::string> saved_dot_scripts;
  for(const auto &entry : dot_scripts)
    saved_dot_scripts.insert(entry.first);
  message_handlert &handler = log.get_message_handler();
  null_message_handlert quiet;
  log.set_message_handler(quiet);
  code_blockt block;
  bool converted = true;
  try
  {
    convert(block);
  }
  catch(const shell_conversion_errort &)
  {
    converted = false;
    live_temporaries.erase(
      live_temporaries.begin() + live, live_temporaries.end());
    loops = saved_loops;
    errexit_ignored = saved_errexit_ignored;
    in_function = saved_in_function;
    current_function = saved_function;
    known_literals = saved_literals;
    for(auto it = dot_scripts.begin(); it != dot_scripts.end();)
    {
      if(saved_dot_scripts.count(it->first) == 0)
        it = dot_scripts.erase(it);
      else
        ++it;
    }
  }
  log.set_message_handler(handler);
  if(converted)
    dest.append(block);
  return converted;
}

/// A simple command whose name is an expansion (XCU 2.9.1.1): the name is
/// expanded first, as one field, and the command is the one of that name
/// at run time. Each function of the script and each modelled builtin is
/// a branch that converts the command with that name; a value that names
/// an unsupported builtin is reported; other values run a utility.
void shell_convertert::convert_dynamic_simple(
  const shell_commandt &command,
  code_blockt &dest)
{
  const source_locationt &loc = command.location;
  // A name whose value is known: the command with its fields as words.
  const shell_wordt &first = command.words.front();
  const bool quoted =
    first.size() == 1 &&
    first.front().kind == shell_word_partt::kindt::DOUBLE_QUOTED &&
    first.front().parts.size() == 1;
  const auto literal =
    first.size() == 1
      ? known_literal(quoted ? first.front().parts.front() : first.front())
      : std::nullopt;
  if(
    literal.has_value() &&
    (quoted || literal->find_first_of("*?[") == std::string::npos))
  {
    std::vector<std::string> fields;
    if(quoted)
      fields.push_back(*literal);
    else
    {
      std::size_t start = literal->find_first_not_of(" \t\n");
      while(start != std::string::npos)
      {
        const std::size_t end = literal->find_first_of(" \t\n", start);
        fields.push_back(literal->substr(
          start, end == std::string::npos ? std::string::npos : end - start));
        start = end == std::string::npos
                  ? end
                  : literal->find_first_not_of(" \t\n", end);
      }
    }
    shell_commandt copy = command;
    copy.words.erase(copy.words.begin());
    for(auto it = fields.rbegin(); it != fields.rend(); ++it)
    {
      shell_word_partt part;
      part.kind = shell_word_partt::kindt::QUOTED_LITERAL;
      part.text = *it;
      part.location = first.front().location;
      copy.words.insert(copy.words.begin(), shell_wordt{part});
    }
    convert_simple(copy, dest);
    return;
  }
  if(contains_at(command.words.front()))
  {
    // "$@" as a command name runs its first field with the others as
    // arguments.
    unsupported(loc, "command name with $@");
  }
  const symbol_exprt name = temporary(string_type(), dest, loc);
  assign(
    dest,
    name,
    expand_word(command.words.front(), shell_word_contextt::FIELD, dest, loc),
    loc);

  // The names a branch is needed for: the functions and the builtins that
  // contain the literal text of the word (for $dir/tool, none).
  std::vector<std::string> literals;
  for(const auto &part : command.words.front())
  {
    const auto text = static_text({part});
    if(text.has_value() && !text->empty())
      literals.push_back(*text);
  }
  std::set<std::string> names;
  const auto add_name = [&](const std::string &candidate)
  {
    // __CPROVER_assert and __CPROVER_assume are written into scripts for
    // verification; a shell would look such a name up as a utility.
    if(candidate.rfind(CPROVER_PREFIX, 0) == 0)
      return;
    for(const auto &literal : literals)
    {
      if(candidate.find(literal) == std::string::npos)
        return;
    }
    names.insert(candidate);
  };
  for(const auto &entry : function_definitions)
    add_name(entry.first);
  for(const auto &entry : builtins)
    add_name(entry.first);
  for(const auto &builtin : unsupported_builtins)
    add_name(builtin);

  // A copy of the command with a literal name, for each branch.
  const auto with_name = [&command](const std::string &text)
  {
    shell_commandt copy = command;
    shell_word_partt part;
    part.kind = shell_word_partt::kindt::QUOTED_LITERAL;
    part.text = text;
    part.location = command.location;
    copy.words.front() = {part};
    return copy;
  };

  // Each branch starts from the state before the command; the branches may
  // change any variable.
  const auto before_dispatch = known_literals;
  // Any other name runs a utility, whose words are only expanded for
  // their side effects.
  code_blockt otherwise;
  convert_non_function(
    with_name(CPROVER_PREFIX "shell_utility"),
    shell_command_kindt::EXTERNAL,
    otherwise);
  std::vector<std::pair<exprt, code_blockt>> branches;
  exprt::operandst unsupported_names;
  std::string unsupported_texts;
  // Refined strings compare the name with each candidate by its length
  // and its bytes, read once each: string equalities of one unknown string
  // with dozens of constants are hard for the string solver.
  const exprt payload = payload_of(name);
  const exprt name_length = string_length(name, dest, loc);
  std::vector<exprt> bytes;
  if(!native())
  {
    std::size_t longest = 0;
    for(const auto &text : names)
      longest = std::max(longest, text.size());
    for(std::size_t i = 0; i < longest; ++i)
    {
      bytes.push_back(string_function(
        ID_cprover_string_char_at_func,
        {payload, from_integer(i, integer_type())},
        unsigned_char_type(),
        dest,
        loc));
    }
  }
  for(const auto &text : names)
  {
    exprt::operandst conditions{
      equal_exprt{name_length, from_integer(text.size(), integer_type())}};
    if(native())
      conditions.push_back(equal(name, string_literal(text, loc), dest, loc));
    else
    {
      for(std::size_t i = 0; i < text.size(); ++i)
      {
        conditions.push_back(equal_exprt{
          bytes[i],
          from_integer(
            static_cast<unsigned char>(text[i]), unsigned_char_type())});
      }
    }
    const exprt is_name = conjunction(conditions);
    code_blockt branch;
    known_literals = before_dispatch;
    // eval and command are converted by convert_simple.
    if(
      (classify(text) == shell_command_kindt::UNSUPPORTED_BUILTIN &&
       text != "eval" && text != "command" &&
       function_definitions.count(text) == 0) ||
      !try_convert(
        [&](code_blockt &block) { convert_simple(with_name(text), block); },
        branch))
    {
      // A builtin that is not supported, or a command that uses something
      // unsupported with these arguments.
      unsupported_names.push_back(is_name);
      unsupported_texts += (unsupported_texts.empty() ? "" : ", ") + text;
      continue;
    }
    branches.emplace_back(is_name, std::move(branch));
  }
  if(!unsupported_names.empty())
  {
    check(
      dest,
      not_exprt{disjunction(unsupported_names)},
      "shell-unsupported",
      "the command name " + display(command.words.front()) + " is none of " +
        unsupported_texts + " (which are not supported here)",
      loc);
  }
  known_literals.clear();
  for(auto it = branches.rbegin(); it != branches.rend(); ++it)
  {
    code_ifthenelset choice{
      it->first, std::move(it->second), std::move(otherwise)};
    otherwise = code_blockt{};
    add(otherwise, std::move(choice), loc);
  }
  dest.append(otherwise);
}

void shell_convertert::convert_non_function(
  const shell_commandt &command,
  shell_command_kindt kind,
  code_blockt &dest)
{
  const source_locationt &loc = command.location;
  const std::string name = *static_text(command.words.front());
  if(kind == shell_command_kindt::UNSUPPORTED_BUILTIN)
  {
    if(function_definitions.count(name) == 0)
      unsupported(loc, "builtin '" + name + "'");
    // A function of this name may be defined at this point; report it if
    // it is not.
    source_locationt check_loc = loc;
    check_loc.set_property_class("shell-unsupported");
    check_loc.set_comment(
      "'" + name + "' is a function here (the builtin '" + name +
      "' is not supported)");
    code_assertt assertion{false_exprt{}};
    assertion.add_source_location() = check_loc;
    dest.add(std::move(assertion));
    add(dest, code_assumet{false_exprt{}}, loc);
    return;
  }
  check_redirections(command, kind);
  if(
    name == "printf" && command.words.size() > 1 &&
    (program.dialect == shell_dialectt::BASH ||
     program.dialect == shell_dialectt::ZSH))
  {
    // printf -v NAME assigns NAME rather than printing.
    const auto first = static_text(command.words[1]);
    if(first.has_value())
    {
      if(first->rfind("-v", 0) == 0)
        unsupported(loc, "printf -v, which assigns a variable");
    }
    else if(has_side_effects(command.words[1]))
      unsupported(loc, "printf with a first operand with side effects");
    else
    {
      // Expanding it twice is the same as once, as it has no side effects.
      // Unquoted, it may split into several fields; that none of them
      // starts with -v holds if the value has no -v at all.
      const exprt value = expand_word(
        command.words[1], shell_word_contextt::CASE_SUBJECT, dest, loc);
      const exprt found = payload_index_of(
        payload_of(value),
        payload_literal("-v"),
        from_integer(0, integer_type()),
        dest,
        loc);
      check(
        dest,
        binary_relation_exprt{found, ID_lt, from_integer(0, integer_type())},
        "shell-unsupported",
        "printf's first operand has no -v (printf -v, which assigns a "
        "variable, is not supported)",
        loc);
    }
  }
  if(
    kind == shell_command_kindt::EXTERNAL ||
    kind == shell_command_kindt::OUTPUT)
  {
    // Their status is nondeterministic whether or not a redirection
    // fails.
    convert_builtin(kind, command, dest);
    return;
  }
  const bool special =
    kind == shell_command_kindt::COLON || kind == shell_command_kindt::EXIT ||
    kind == shell_command_kindt::EXPORT || kind == shell_command_kindt::UNSET ||
    kind == shell_command_kindt::BREAK ||
    kind == shell_command_kindt::CONTINUE ||
    kind == shell_command_kindt::SHIFT || kind == shell_command_kindt::SET ||
    kind == shell_command_kindt::RETURN || kind == shell_command_kindt::EXEC ||
    kind == shell_command_kindt::DOT;
  with_redirections(
    command,
    special,
    dest,
    [&](code_blockt &block) { convert_builtin(kind, command, block); });
}

void shell_convertert::convert_builtin(
  shell_command_kindt kind,
  const shell_commandt &command,
  code_blockt &dest)
{
  const source_locationt &loc = command.location;
  // Special builtins keep the assignments before them (XCU 2.9.1.2),
  // except in bash (outside POSIX mode) and zsh, where they only last for
  // the command, as for other builtins.
  const bool persistent_assignments = program.dialect != shell_dialectt::BASH &&
                                      program.dialect != shell_dialectt::ZSH;
  const bool special =
    kind == shell_command_kindt::COLON || kind == shell_command_kindt::EXIT ||
    kind == shell_command_kindt::EXPORT || kind == shell_command_kindt::UNSET ||
    kind == shell_command_kindt::BREAK ||
    kind == shell_command_kindt::CONTINUE ||
    kind == shell_command_kindt::SHIFT || kind == shell_command_kindt::SET ||
    kind == shell_command_kindt::RETURN || kind == shell_command_kindt::EXEC ||
    kind == shell_command_kindt::DOT;
  if(
    !command.assignments.empty() && kind != shell_command_kindt::EXTERNAL &&
    kind != shell_command_kindt::OUTPUT && !special)
  {
    // Shells differ in whether these assignments persist.
    unsupported(loc, "variable assignment before a regular builtin");
  }
  if(
    special && !persistent_assignments && !command.assignments.empty() &&
    (kind == shell_command_kindt::EXPORT || kind == shell_command_kindt::UNSET))
  {
    unsupported(loc, "temporary variable assignment before export or unset");
  }
  if(special && !persistent_assignments)
  {
    // Only evaluated for their side effects.
    for(const auto &assignment : command.assignments)
      expand_word(assignment.value, shell_word_contextt::IGNORED, dest, loc);
  }
  if(special && persistent_assignments && !command.assignments.empty())
  {
    // The arguments are expanded before the assignments (XCU 2.9.1.1);
    // with literal arguments the order does not matter, and the
    // assignments can be made first.
    for(std::size_t i = 1; i < command.words.size(); ++i)
    {
      if(!static_text(command.words[i]).has_value())
      {
        unsupported(
          loc,
          "variable assignment before a special builtin with expanded "
          "arguments");
      }
    }
    for(const auto &assignment : command.assignments)
    {
      const exprt value = expand_word(
        assignment.value,
        shell_word_contextt::ASSIGNMENT,
        dest,
        assignment.location);
      assign_variable(assignment.name, value, dest, assignment.location);
    }
  }

  switch(kind)
  {
  case shell_command_kindt::TEST:
  case shell_command_kindt::BRACKET:
    convert_test_command(command, kind == shell_command_kindt::BRACKET, dest);
    return;
  case shell_command_kindt::ASSERT:
  case shell_command_kindt::ASSUME:
    convert_verification_command(
      command, kind == shell_command_kindt::ASSERT, dest);
    return;
  case shell_command_kindt::EXIT:
    convert_exit(command, dest);
    return;
  case shell_command_kindt::EXPORT:
    convert_export(command, dest);
    return;
  case shell_command_kindt::UNSET:
    convert_unset(command, dest);
    return;
  case shell_command_kindt::BREAK:
  case shell_command_kindt::CONTINUE:
    convert_break_continue(command, kind == shell_command_kindt::BREAK, dest);
    return;
  case shell_command_kindt::SHIFT:
    convert_shift(command, dest);
    return;
  case shell_command_kindt::SET:
    convert_set(command, dest);
    return;
  case shell_command_kindt::RETURN:
    convert_return(command, dest);
    return;
  case shell_command_kindt::EXEC:
    convert_exec(command, dest);
    return;
  case shell_command_kindt::DOT:
    convert_dot(command, dest);
    return;
  case shell_command_kindt::COLON:
  case shell_command_kindt::TRUE:
  case shell_command_kindt::FALSE:
  case shell_command_kindt::OUTPUT:
  case shell_command_kindt::EXTERNAL:
    break;
  case shell_command_kindt::UNSUPPORTED_BUILTIN:
    UNREACHABLE;
  }

  // The remaining commands only depend on their arguments for output.
  for(std::size_t i = 1; i < command.words.size(); ++i)
    expand_word(command.words[i], shell_word_contextt::IGNORED, dest, loc);
  if(!special)
  {
    // In the environment of the command only.
    for(const auto &assignment : command.assignments)
      expand_word(assignment.value, shell_word_contextt::IGNORED, dest, loc);
  }
  if(kind == shell_command_kindt::COLON || kind == shell_command_kindt::TRUE)
    set_status(dest, 0, loc);
  else if(kind == shell_command_kindt::FALSE)
    set_status(dest, 1, loc);
  else
    nondet_status(dest, 255, loc);
}

void shell_convertert::convert_test_command(
  const shell_commandt &command,
  bool bracket,
  code_blockt &dest)
{
  const source_locationt &loc = command.location;
  std::size_t end = command.words.size();
  if(bracket)
  {
    auto last = static_text(command.words.back());
    if(command.words.size() < 2 || !last.has_value() || *last != "]")
      unsupported(loc, "'[' without a closing ']'");
    --end;
  }
  std::vector<test_argumentt> args;
  for(std::size_t i = 1; i < end; ++i)
  {
    const auto &word = command.words[i];
    args.push_back(
      {expand_word(word, shell_word_contextt::FIELD, dest, loc),
       static_text(word)});
  }
  test(std::move(args), dest, loc);
}

/// __CPROVER_assert and __CPROVER_assume take the arguments of test.
void shell_convertert::convert_verification_command(
  const shell_commandt &command,
  bool assertion,
  code_blockt &dest)
{
  const source_locationt &loc = command.location;
  std::vector<test_argumentt> args;
  std::string description;
  for(std::size_t i = 1; i < command.words.size(); ++i)
  {
    const auto &word = command.words[i];
    args.push_back(
      {expand_word(word, shell_word_contextt::FIELD, dest, loc),
       static_text(word)});
    description += (i > 1 ? " " : "") + display(word);
  }
  test(std::move(args), dest, loc);
  const exprt holds = equal_exprt{status(), from_integer(0, status_type())};
  if(assertion)
  {
    source_locationt assert_loc = loc;
    assert_loc.set_property_class("assertion");
    assert_loc.set_comment("assertion " + description);
    code_assertt check{holds};
    check.add_source_location() = assert_loc;
    dest.add(std::move(check));
  }
  else
    add(dest, code_assumet{holds}, loc);
  set_status(dest, 0, loc);
}

void shell_convertert::convert_exit(
  const shell_commandt &command,
  code_blockt &dest)
{
  const source_locationt &loc = command.location;
  if(command.words.size() > 2)
    unsupported(loc, "exit with several operands");
  if(command.words.size() == 2)
    status_from_operand(command.words[1], "exit", true, dest, loc);
  exit_shell(dest, false, loc);
}

/// exec [command [argument...]] (XCU 2.15 exec): without a command, the
/// redirections apply to the shell, which only affects output; with one,
/// the command replaces the shell, which then exits with its status.
void shell_convertert::convert_exec(
  const shell_commandt &command,
  code_blockt &dest)
{
  const source_locationt &loc = command.location;
  if(command.words.size() == 1)
  {
    set_status(dest, 0, loc);
    return;
  }
  const auto name = static_text(command.words[1]);
  if(name.has_value() && !name->empty() && (*name)[0] == '-')
    unsupported(loc, "exec option " + *name);
  if(
    name.has_value() && function_definitions.count(*name) != 0 &&
    (program.dialect == shell_dialectt::MKSH ||
     program.dialect == shell_dialectt::ZSH))
  {
    // mksh and zsh run functions; dash and bash look for a utility.
    unsupported(loc, "exec of a shell function");
  }
  for(std::size_t i = 1; i < command.words.size(); ++i)
    expand_word(command.words[i], shell_word_contextt::IGNORED, dest, loc);
  // The utility's status, which is 127 if it is not found.
  nondet_status(dest, 255, loc);
  exit_shell(dest, false, loc);
}

void shell_convertert::exit_shell(
  code_blockt &dest,
  bool failure,
  const source_locationt &loc)
{
  if(failure)
  {
    nondet_status(dest, 255, loc);
    add(
      dest,
      code_assumet{notequal_exprt{status(), from_integer(0, status_type())}},
      loc);
  }
  assign(dest, exited(), true_exprt{}, loc);
  add(dest, code_frontend_returnt{}, loc);
}

void shell_convertert::convert_export(
  const shell_commandt &command,
  code_blockt &dest)
{
  const source_locationt &loc = command.location;
  for(std::size_t i = 1; i < command.words.size(); ++i)
  {
    const shell_wordt &word = command.words[i];
    // NAME or NAME=value, where the name is literal text.
    if(word.empty() || word[0].kind != shell_word_partt::kindt::LITERAL)
      unsupported(loc, "export operand that is not NAME or NAME=value");
    const std::string &first = word[0].text;
    const std::size_t equals = first.find('=');
    const std::string name = first.substr(0, equals);
    if(
      name.empty() || name[0] == '-' ||
      name.find_first_not_of("abcdefghijklmnopqrstuvwxyz"
                             "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") !=
        std::string::npos)
    {
      unsupported(loc, "export operand that is not NAME or NAME=value");
    }
    if(equals == std::string::npos)
    {
      if(word.size() != 1)
        unsupported(loc, "export operand that is not NAME or NAME=value");
      // Exporting only matters to child processes, which are not
      // modelled.
      check_parameter_name(name, loc);
      continue;
    }
    shell_wordt value_word = word;
    value_word[0].text = first.substr(equals + 1);
    const exprt value =
      expand_word(value_word, shell_word_contextt::ASSIGNMENT, dest, loc);
    assign_variable(name, value, dest, loc);
  }
  set_status(dest, 0, loc);
}

void shell_convertert::convert_unset(
  const shell_commandt &command,
  code_blockt &dest)
{
  const source_locationt &loc = command.location;
  std::size_t first = 1;
  if(command.words.size() > 1)
  {
    auto option = static_text(command.words[1]);
    if(option.has_value() && *option == "-v")
      first = 2;
    else if(option.has_value() && !option->empty() && (*option)[0] == '-')
      unsupported(loc, "unset option " + *option);
  }
  for(std::size_t i = first; i < command.words.size(); ++i)
  {
    auto name = static_text(command.words[i]);
    if(!name.has_value())
      unsupported(loc, "unset operand that is an expansion");
    const symbol_exprt var = variable(*name, loc);
    known_literals.erase(*name);
    assign(dest, var, string_literal("", loc), loc);
    assign(dest, set_of(var), from_integer(0, component_type("set")), loc);
  }
  set_status(dest, 0, loc);
}
