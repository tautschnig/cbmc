/// \file
/// Shell converter: regular builtins whose effects on the file system and
/// the process are not modelled: cd (XCU cd), umask (XCU umask), and type
/// (XCU type).

#include "shell_convert_class.h"

#include <regex>

void shell_convertert::convert_cd(
  const shell_commandt &command,
  code_blockt &dest)
{
  const source_locationt &loc = command.location;
  // The working directory is not modelled: pathnames that depend on it,
  // such as those of dot scripts, are not supported elsewhere.
  for(std::size_t i = 1; i < command.words.size(); ++i)
    expand_word(command.words[i], shell_word_contextt::IGNORED, dest, loc);
  // Success: PWD is the new working directory, and OLDPWD the previous one
  // (XCU cd), neither of which is known.
  code_blockt success;
  assign_variable("OLDPWD", nondet_string(success, loc), success, loc);
  const exprt pwd = nondet_string(success, loc);
  add(
    success,
    code_assumet{
      payload_has_prefix(payload_of(pwd), payload_literal("/"), success, loc)},
    loc);
  assign_variable("PWD", pwd, success, loc);
  set_status(success, 0, loc);
  code_blockt failure;
  nondet_status(failure, 255, loc);
  add(
    failure,
    code_assumet{notequal_exprt{status(), from_integer(0, status_type())}},
    loc);
  add(
    dest,
    code_ifthenelset{
      nondet(bool_typet{}, loc), std::move(success), std::move(failure)},
    loc);
}

void shell_convertert::convert_umask(
  const shell_commandt &command,
  code_blockt &dest)
{
  const source_locationt &loc = command.location;
  if(command.words.size() == 2)
  {
    // Setting a mask given as literal text that all shells accept: octal
    // digits, or a symbolic mode (XCU chmod).
    const auto mask = static_text(command.words[1]);
    static const std::regex octal{"[0-7]{1,4}"};
    static const std::regex symbolic{
      "[ugoa]*[-+=][rwx]*(,[ugoa]*[-+=][rwx]*)*"};
    if(
      mask.has_value() &&
      (std::regex_match(*mask, octal) || std::regex_match(*mask, symbolic)))
    {
      set_status(dest, 0, loc);
      return;
    }
  }
  // Other operands may be invalid, and without operands umask writes the
  // mask, which can fail.
  for(std::size_t i = 1; i < command.words.size(); ++i)
    expand_word(command.words[i], shell_word_contextt::IGNORED, dest, loc);
  nondet_status(dest, 255, loc);
}

void shell_convertert::convert_type(
  const shell_commandt &command,
  code_blockt &dest)
{
  const source_locationt &loc = command.location;
  if(command.words.size() < 2)
    unsupported(loc, "type without a name");
  // Whether each name is found; the status is that of a missing name if
  // any is missing.
  exprt::operandst found;
  for(std::size_t i = 1; i < command.words.size(); ++i)
  {
    const auto name = static_text(command.words[i]);
    // Options differ between shells.
    if(!name.has_value() || name->empty() || (*name)[0] == '-')
      unsupported(loc, "type with an operand other than a literal name");
    if(is_builtin_everywhere(*name))
      continue;
    const symbol_exprt exists = temporary(bool_typet{}, dest, loc);
    assign(dest, exists, nondet(bool_typet{}, loc), loc);
    if(function_definitions.count(*name) != 0)
    {
      found.push_back(or_exprt{
        notequal_exprt{
          current_definition(*name),
          from_integer(0, current_definition_type())},
        exists});
    }
    else
      found.push_back(exists);
  }
  // type writes a description of each name, which can fail.
  code_blockt all_found;
  nondet_status(all_found, 255, loc);
  const exprt missing = not_found_status(dest, loc);
  code_blockt some_missing;
  assign(some_missing, status(), missing, loc);
  add(
    dest,
    code_ifthenelset{
      conjunction(found), std::move(all_found), std::move(some_missing)},
    loc);
}
