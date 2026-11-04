/// \file
/// Shell converter: the command builtin (XCU command).

#include "shell_convert_class.h"

exprt shell_convertert::not_found_status(
  code_blockt &dest,
  const source_locationt &loc)
{
  // dash and busybox: 127; bash (also as sh), mksh and zsh: 1.
  if(program.dialect != shell_dialectt::POSIX)
    return from_integer(1, status_type());
  const symbol_exprt status = temporary(status_type(), dest, loc);
  assign(
    dest,
    status,
    if_exprt{
      nondet(bool_typet{}, loc),
      from_integer(127, status_type()),
      from_integer(1, status_type())},
    loc);
  return status;
}

/// Builtins that every modelled dialect has, which command -v finds.
bool is_builtin_everywhere(const std::string &name)
{
  static const std::set<std::string> names{
    ":",       "break",    "continue", ".",    "eval",    "exec",  "exit",
    "export",  "readonly", "return",   "set",  "shift",   "times", "trap",
    "unset",   "alias",    "bg",       "cd",   "command", "false", "fg",
    "getopts", "jobs",     "kill",     "read", "true",    "umask", "unalias",
    "wait",    "test",     "[",        "echo", "printf",  "pwd"};
  return names.count(name) != 0;
}

void shell_convertert::convert_command_builtin(
  const shell_commandt &command,
  code_blockt &dest)
{
  const source_locationt &loc = command.location;
  if(!command.assignments.empty())
    unsupported(loc, "variable assignment before command");
  // The options: -p (the default PATH, which does not change the model),
  // -v and -V (describe NAME instead of running it).
  bool describe = false;
  bool dynamic_operand = false;
  std::size_t first = 1;
  for(; first < command.words.size(); ++first)
  {
    const auto option = known_text(command.words[first]);
    if(!option.has_value())
    {
      // An operand, unless its value starts with -, which is checked
      // below.
      dynamic_operand = true;
      break;
    }
    if(*option == "--")
    {
      ++first;
      break;
    }
    if(option->size() < 2 || (*option)[0] != '-')
      break;
    for(char letter : option->substr(1))
    {
      if(letter == 'v' || letter == 'V')
        describe = true;
      else if(letter != 'p')
        unsupported(loc, "command -" + std::string{letter});
    }
  }

  if(!describe)
  {
    // command NAME ARGS runs NAME as a builtin or utility, not as a
    // function (XCU command).
    if(first == command.words.size())
    {
      set_status(dest, 0, loc);
      return;
    }
    const auto name = known_text(command.words[first]);
    if(!name.has_value())
      unsupported(loc, "command with a command name that is an expansion");
    const shell_command_kindt kind = classify(*name);
    if(
      kind != shell_command_kindt::EXTERNAL &&
      kind != shell_command_kindt::OUTPUT &&
      kind != shell_command_kindt::TEST &&
      kind != shell_command_kindt::BRACKET &&
      kind != shell_command_kindt::TRUE && kind != shell_command_kindt::FALSE)
    {
      // Special builtins lose their special properties under command.
      unsupported(loc, "command with the builtin '" + *name + "'");
    }
    shell_commandt copy = command;
    copy.words.erase(copy.words.begin(), copy.words.begin() + first);
    shell_word_partt literal;
    literal.kind = shell_word_partt::kindt::QUOTED_LITERAL;
    literal.text = *name;
    literal.location = loc;
    copy.words.front() = {literal};
    convert_non_function(copy, kind, dest);
    return;
  }

  // command -v NAME: status 0 if NAME is a function, builtin, or utility.
  if(command.words.size() != first + 1)
    unsupported(loc, "command -v without exactly one name");
  check_redirections(command, shell_command_kindt::EXTERNAL);
  with_redirections(
    command,
    false,
    dest,
    [&](code_blockt &block)
    {
      const exprt missing = not_found_status(block, loc);
      const shell_wordt &word = command.words[first];
      const auto name = known_text(word);
      // Whether a utility of this name exists is not known: found or not.
      code_blockt maybe;
      const symbol_exprt found = temporary(bool_typet{}, maybe, loc);
      assign(maybe, found, nondet(bool_typet{}, loc), loc);
      assign(
        maybe,
        status(),
        if_exprt{found, from_integer(0, status_type()), missing},
        loc);
      if(!name.has_value())
      {
        if(dynamic_operand)
        {
          const exprt value =
            expand_word(word, shell_word_contextt::FIELD, block, loc);
          check(
            block,
            not_exprt{payload_has_prefix(
              payload_of(value), payload_literal("-"), block, loc)},
            "shell-unsupported",
            "the operand " + display(word) +
              " of command does not start with - (it would be an option)",
            loc);
        }
        else
          expand_word(word, shell_word_contextt::IGNORED, block, loc);
        block.append(maybe);
        return;
      }
      if(is_builtin_everywhere(*name))
      {
        set_status(block, 0, loc);
        return;
      }
      if(function_definitions.count(*name) != 0)
      {
        code_blockt defined;
        set_status(defined, 0, loc);
        add(
          block,
          code_ifthenelset{
            equal_exprt{
              current_definition(*name),
              from_integer(0, current_definition_type())},
            std::move(maybe),
            std::move(defined)},
          loc);
        return;
      }
      block.append(maybe);
    });
}
