/// \file
/// Shell converter: read (XCU read). The input is not modelled: the
/// variables get values that read can produce from some line, and the
/// status is that of reading some input.

#include "shell_convert_class.h"

#include <algorithm>
#include <cctype>

std::optional<std::pair<bool, std::vector<std::string>>>
shell_convertert::read_operands(const shell_commandt &command)
{
  bool raw = false;
  std::vector<std::string> names;
  bool options = true;
  for(std::size_t i = 1; i < command.words.size(); ++i)
  {
    const auto text = static_text(command.words[i]);
    if(!text.has_value())
      return {};
    if(options && *text == "--")
    {
      options = false;
      continue;
    }
    if(options && text->size() > 1 && (*text)[0] == '-')
    {
      // -r, possibly repeated: other options differ between shells.
      if(text->find_first_not_of('r', 1) != std::string::npos)
        return {};
      raw = true;
      continue;
    }
    options = false;
    if(
      text->empty() ||
      !(std::isalpha(static_cast<unsigned char>((*text)[0])) ||
        (*text)[0] == '_') ||
      text->find_first_not_of(
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") !=
        std::string::npos)
    {
      return {};
    }
    names.push_back(*text);
  }
  return {{raw, names}};
}

exprt shell_convertert::read_value(
  bool raw,
  bool last,
  const std::optional<std::string> &ifs,
  code_blockt &dest,
  const source_locationt &loc)
{
  const exprt value = nondet_string(dest, loc);
  // The line ends at a newline, and a backslash before a newline (without
  // -r) continues it: the values have no newline.
  std::bitset<256> excluded;
  excluded.set('\n');
  // With -r, the variables but the last get one field each, which has no
  // byte of IFS; without -r, a backslash can escape bytes of IFS.
  if(raw && !last && ifs.has_value())
    excluded |= ifs_set(*ifs);
  add(
    dest,
    code_assumet{
      not_exprt{payload_contains_byte(payload_of(value), excluded, dest, loc)}},
    loc);
  return value;
}

void shell_convertert::convert_read(
  const shell_commandt &command,
  code_blockt &dest)
{
  const source_locationt &loc = command.location;
  const auto operands = read_operands(command);
  if(!operands.has_value())
    unsupported(loc, "read other than read [-r] [--] NAME...");
  const bool raw = operands->first;
  const std::vector<std::string> &names = operands->second;
  // Without names, bash, busybox, mksh and zsh assign REPLY (the whole
  // line in bash and busybox, without leading and trailing IFS white
  // space in mksh and zsh: either way a value without newlines); dash
  // reports an error, which is unsupported.
  std::vector<std::string> reply{"REPLY"};
  if(names.empty() && program.dialect == shell_dialectt::POSIX)
    unsupported(loc, "read without a variable name");
  const std::vector<std::string> &targets = names.empty() ? reply : names;

  // Assignments before read only last for it in all shells (XCU 2.9.1.2):
  // IFS, with a value known here, changes how read splits the line;
  // others have no effect on it.
  std::optional<std::string> ifs = known_ifs();
  for(const auto &assignment : command.assignments)
  {
    const exprt value = expand_word(
      assignment.value,
      shell_word_contextt::ASSIGNMENT,
      dest,
      assignment.location);
    check_parameter_name(assignment.name, assignment.location);
    if(
      std::find(targets.begin(), targets.end(), assignment.name) !=
      targets.end())
    {
      unsupported(
        assignment.location, "assignment before read of a variable it reads");
    }
    if(assignment.name == "IFS")
    {
      ifs = literal_of(value);
      if(!ifs.has_value())
      {
        unsupported(
          assignment.location,
          "assignment of a value that is not known here to IFS");
      }
    }
  }

  // Successful: a line was read, and each variable is assigned.
  code_blockt success;
  for(std::size_t i = 0; i < targets.size(); ++i)
  {
    const bool last = i + 1 == targets.size();
    assign_variable(
      targets[i], read_value(raw, last, ifs, success, loc), success, loc);
  }
  set_status(success, 0, loc);
  const auto after_success = known_literals;

  // At the end of the input, or after an error: a positive status, and
  // each variable is assigned what was read, if anything, or kept (as
  // bash does where the input cannot be read).
  code_blockt failure;
  for(std::size_t i = 0; i < targets.size(); ++i)
  {
    const bool last = i + 1 == targets.size();
    code_blockt assign_it;
    assign_variable(
      targets[i], read_value(raw, last, ifs, assign_it, loc), assign_it, loc);
    add(
      failure,
      code_ifthenelset{nondet(bool_typet{}, loc), std::move(assign_it)},
      loc);
  }
  nondet_status(failure, 255, loc);
  add(
    failure,
    code_assumet{
      not_exprt{equal_exprt{status(), from_integer(0, status_type())}}},
    loc);
  // Both forget the values of the variables.
  INVARIANT(
    known_literals == after_success,
    "read assigns values not known during conversion");

  add(
    dest,
    code_ifthenelset{
      nondet(bool_typet{}, loc), std::move(success), std::move(failure)},
    loc);
}
