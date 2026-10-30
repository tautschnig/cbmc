/// \file
/// Shell converter: commands (POSIX.1-2024 XCU 2.9).

#include "shell_convert_class.h"

// ------------------------------------------------------------------
// Commands

shell_command_kindt shell_convertert::classify(const std::string &name) const
{
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
    {CPROVER_PREFIX "assert", shell_command_kindt::ASSERT},
    {CPROVER_PREFIX "assume", shell_command_kindt::ASSUME},
    {"echo", shell_command_kindt::OUTPUT},
    {"printf", shell_command_kindt::OUTPUT}};
  // Builtins that change the shell environment (XCU 2.15 special
  // builtins, regular builtins of XCU 1.7, and common extensions).
  static const std::set<std::string> unsupported_builtins{
    ".",        "eval",     "exec",    "readonly",  "return",  "times",
    "trap",     "cd",       "read",    "getopts",   "alias",   "unalias",
    "umask",    "wait",     "jobs",    "fg",        "bg",      "command",
    "type",     "hash",     "kill",    "ulimit",    "fc",      "newgrp",
    "local",    "declare",  "typeset", "source",    "let",     "shopt",
    "builtin",  "enable",   "mapfile", "readarray", "pushd",   "popd",
    "dirs",     "disown",   "suspend", "logout",    "nameref", "autoload",
    "setopt",   "unsetopt", "emulate", "print",     "whence",  "functions",
    "zmodload", "caller",   "compgen", "complete"};
  const auto it = builtins.find(name);
  if(it != builtins.end())
    return it->second;
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
  check_parameter_name(name, loc);
  // Expansion results are always set.
  assign(dest, variable(name), value, loc);
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
  using kindt = shell_commandt::kindt;
  if(command.background)
  {
    convert_background(command, dest);
    return;
  }
  if(!command.redirects.empty() && command.kind != kindt::SIMPLE)
    unsupported(command.location, "redirection of a compound command");

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
    unsupported(command.location, "subshell ( ... )");
  case kindt::FUNCTION_DEFINITION:
    unsupported(command.location, "function definition");
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
  convert_command(command.body[0], dest);
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
  convert_list(command.condition, dest);
  code_blockt then_block;
  convert_list(command.body, then_block);
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
  convert_list(command.condition, dest);
  code_blockt leave;
  // The status is that of the last body executed, or zero.
  assign(leave, status(), loop_status, loc);
  goto_label(leave, break_label);
  add(dest, if_status_zero(is_until, std::move(leave)), loc);

  loops.push_back({break_label, continue_label, loop_status});
  convert_list(command.body, dest);
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
    dynamic |= contains_at(word);
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
    assign_variable(command.name, item, dest, loc);
    loops.push_back({break_label, continue_label, loop_status});
    convert_list(command.body, dest);
    loops.pop_back();
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
  assign_variable(
    command.name,
    index_exprt{
      items.array, typecast_exprt::conditional_cast(index, c_index_type())},
    dest,
    loc);
  loops.push_back({break_label, continue_label, loop_status});
  convert_list(command.body, dest);
  loops.pop_back();
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

  const irep_idt end_label = new_label("esac");
  std::vector<irep_idt> body_labels;
  for(const auto &item : command.case_items)
  {
    body_labels.push_back(new_label("case_body"));
    for(const auto &pattern : item.patterns)
    {
      const exprt matches =
        case_pattern_matches(pattern, subject, dest, item.location);
      code_blockt jump;
      goto_label(jump, body_labels.back());
      add(dest, code_ifthenelset{matches, std::move(jump)}, item.location);
    }
  }
  // No pattern matched: the status is zero.
  set_status(dest, 0, loc);
  goto_label(dest, end_label);

  for(std::size_t i = 0; i < command.case_items.size(); ++i)
  {
    const auto &item = command.case_items[i];
    label(dest, body_labels[i]);
    set_status(dest, 0, item.location);
    convert_list(item.body, dest);
    if(item.terminator == ";;" || i + 1 == command.case_items.size())
      goto_label(dest, end_label);
  }
  label(dest, end_label);
}

/// Whether \p pattern matches; only patterns without unquoted pattern
/// characters, and the pattern "*", are supported.
exprt shell_convertert::case_pattern_matches(
  const shell_wordt &pattern,
  const exprt &subject,
  code_blockt &dest,
  const source_locationt &loc)
{
  if(
    pattern.size() == 1 &&
    pattern[0].kind == shell_word_partt::kindt::LITERAL &&
    pattern[0].text == "*")
  {
    return true_exprt{};
  }
  const exprt text =
    expand_word(pattern, shell_word_contextt::FIELD, dest, loc);
  return equal(subject, text, dest, loc);
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
    if(
      kind != shell_command_kindt::EXTERNAL &&
      kind != shell_command_kindt::OUTPUT && !is_harmless_redirection(redirect))
    {
      // A failed redirection changes the status of builtins, and makes
      // the shell exit for special builtins (XCU 2.8.1).
      unsupported(
        redirect.location, "redirection of a builtin that could fail");
    }
  }
}

/// Commands in pipelines and asynchronous lists run in subshells: they
/// must not have effects that would wrongly be applied to this shell.
void shell_convertert::check_pure_command(const shell_commandt &command)
{
  if(command.kind != shell_commandt::kindt::SIMPLE || command.words.empty())
    unsupported(
      command.location,
      "pipeline or & with a command other than a "
      "simple command");
  auto name = static_text(command.words.front());
  if(!name.has_value())
    unsupported(command.location, "command name that is an expansion");
  const shell_command_kindt kind = classify(*name);
  if(
    kind != shell_command_kindt::EXTERNAL &&
    kind != shell_command_kindt::OUTPUT && kind != shell_command_kindt::TEST &&
    kind != shell_command_kindt::BRACKET && kind != shell_command_kindt::TRUE &&
    kind != shell_command_kindt::FALSE && kind != shell_command_kindt::COLON)
  {
    unsupported(command.location, "'" + *name + "' in a pipeline or with &");
  }
  for(const auto &word : command.words)
  {
    if(has_side_effects(word))
      unsupported(
        command.location,
        "expansion with side effects in a "
        "pipeline or with &");
  }
  for(const auto &assignment : command.assignments)
  {
    if(has_side_effects(assignment.value))
      unsupported(
        command.location,
        "expansion with side effects in a "
        "pipeline or with &");
  }
  check_redirections(command, kind);
}

/// The status of a pipeline is that of its last command (XCU 2.9.2); each
/// command runs in a subshell environment.
void shell_convertert::convert_pipeline(
  const shell_commandt &command,
  code_blockt &dest)
{
  for(const auto &element : command.body)
  {
    if(element.negated || element.background)
      unsupported(element.location, "! or & inside a pipeline");
    check_pure_command(element);
  }
  // The commands have no effects on this shell, so only the last one's
  // status is computed.
  shell_commandt last = command.body.back();
  last.redirects.clear();
  last.assignments.clear();
  convert_simple(last, dest);
}

void shell_convertert::convert_background(
  const shell_commandt &command,
  code_blockt &dest)
{
  if(command.kind == shell_commandt::kindt::PIPELINE)
  {
    for(const auto &element : command.body)
      check_pure_command(element);
  }
  else
    check_pure_command(command);
  // The status of an asynchronous list is zero (XCU 2.9.3.1).
  set_status(dest, 0, command.location);
}

void shell_convertert::convert_assignments_only(
  const shell_commandt &command,
  code_blockt &dest)
{
  const source_locationt &loc = command.location;
  if(!command.redirects.empty())
    unsupported(loc, "redirection without a command name");
  expanded_command_substitution = false;
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
  if(expanded_command_substitution)
    nondet_status(dest, 255, loc);
  else
    set_status(dest, 0, loc);
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
    unsupported(loc, "command name that is an expansion");
  const shell_command_kindt kind = classify(*name);
  if(kind == shell_command_kindt::UNSUPPORTED_BUILTIN)
    unsupported(loc, "builtin '" + *name + "'");
  check_redirections(command, kind);
  convert_builtin(kind, command, dest);
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
    kind == shell_command_kindt::SHIFT || kind == shell_command_kindt::SET;
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
  {
    // The value is only used for the status, which nothing observes yet.
    auto text = static_text(command.words[1]);
    if(
      text.has_value() && !text->empty() && text->size() < 10 &&
      text->find_first_not_of("0123456789") == std::string::npos)
    {
      set_status(dest, std::stoul(*text) % 256, loc);
    }
    else
    {
      expand_word(command.words[1], shell_word_contextt::IGNORED, dest, loc);
      nondet_status(dest, 255, loc);
    }
  }
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
    check_parameter_name(*name, loc);
    const symbol_exprt var = variable(*name);
    assign(dest, var, string_literal("", loc), loc);
    assign(dest, set_of(var), from_integer(0, component_type("set")), loc);
  }
  set_status(dest, 0, loc);
}
