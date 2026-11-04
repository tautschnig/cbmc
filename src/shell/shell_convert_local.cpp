/// \file
/// Shell converter: local (dash, bash, busybox, mksh, zsh; not POSIX).
/// local NAME makes NAME local to the function call: its value is
/// restored when the function returns. A function's body saves, on entry,
/// the variables that it makes local anywhere, and restores those that it
/// did make local at its end, which its returns jump to.

#include "shell_convert_class.h"

#include <cctype>

std::optional<std::pair<std::string, std::optional<shell_wordt>>>
shell_convertert::local_operand(const shell_wordt &word)
{
  using kindt = shell_word_partt::kindt;
  if(word.empty() || word.front().kind != kindt::LITERAL)
    return {};
  const std::string &text = word.front().text;
  const std::size_t equal = text.find('=');
  const std::string name = text.substr(0, equal);
  if(
    name.empty() ||
    !(std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_') ||
    name.find_first_not_of(
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") !=
      std::string::npos)
  {
    return {};
  }
  if(equal == std::string::npos)
  {
    if(word.size() != 1)
      return {};
    return {{name, std::nullopt}};
  }
  shell_wordt value = word;
  value.front().text = text.substr(equal + 1);
  if(value.front().text.empty())
    value.erase(value.begin());
  return {{name, value}};
}

void shell_convertert::collect_local_names(
  const shell_command_listt &list,
  std::set<std::string> &names)
{
  using kindt = shell_commandt::kindt;
  for(const auto &command : list)
  {
    // Function definitions have their own locals.
    if(command.kind == kindt::FUNCTION_DEFINITION)
      continue;
    if(command.kind == kindt::SIMPLE && command.words.size() > 1)
    {
      const auto name = static_text(command.words.front());
      if(name.has_value() && *name == "local")
      {
        for(std::size_t i = 1; i < command.words.size(); ++i)
        {
          if(const auto operand = local_operand(command.words[i]))
            names.insert(operand->first);
        }
      }
    }
    for(const auto *sub :
        {&command.body, &command.condition, &command.else_body})
    {
      collect_local_names(*sub, names);
    }
    for(const auto &item : command.case_items)
      collect_local_names(item.body, names);
  }
}

void shell_convertert::convert_local(
  const shell_commandt &command,
  code_blockt &dest)
{
  const source_locationt &loc = command.location;
  if(!in_function || !function_locals.has_value())
    unsupported(loc, "local outside a function");
  if(!command.assignments.empty())
    unsupported(loc, "variable assignment before local");
  if(redirection_may_fail(command))
    unsupported(loc, "local with a redirection that can fail");
  for(std::size_t i = 1; i < command.words.size(); ++i)
  {
    const auto operand = local_operand(command.words[i]);
    if(!operand.has_value())
      unsupported(loc, "local with an operand that is not NAME or NAME=value");
    const std::string &name = operand->first;
    // The names were collected from the function's body; others come
    // from eval or a command name that is an expansion.
    const auto local = function_locals->find(name);
    if(local == function_locals->end())
      unsupported(
        loc, "local of a name that the function's body does not show");
    assign(dest, local->second.second, true_exprt{}, loc);
    if(operand->second.has_value())
    {
      // Not subject to field splitting in any shell.
      const exprt value = expand_word(
        *operand->second, shell_word_contextt::ASSIGNMENT, dest, loc);
      assign_variable(name, value, dest, loc);
    }
    else if(program.dialect == shell_dialectt::ZSH)
      assign_variable(name, string_literal("", loc), dest, loc);
    else if(program.dialect != shell_dialectt::POSIX)
    {
      // bash, busybox and mksh: unset in the function; dash keeps the
      // value.
      const symbol_exprt var = variable(name, loc);
      known_literals.erase(name);
      assign(dest, var, unset_string(loc), loc);
    }
  }
  set_status(dest, 0, loc);
}

void shell_convertert::save_locals(
  const shell_command_listt &body,
  code_blockt &dest,
  const source_locationt &loc)
{
  std::set<std::string> names;
  collect_local_names(body, names);
  function_locals.emplace();
  for(const auto &name : names)
  {
    const symbol_exprt saved = temporary(string_type(), dest, loc);
    assign(dest, saved, variable(name, loc), loc);
    const symbol_exprt made_local = temporary(bool_typet{}, dest, loc);
    assign(dest, made_local, false_exprt{}, loc);
    function_locals->emplace(name, std::make_pair(saved, made_local));
  }
}

void shell_convertert::restore_locals(
  code_blockt &dest,
  const source_locationt &loc)
{
  for(const auto &entry : *function_locals)
  {
    code_blockt restore;
    assign(restore, variable(entry.first, loc), entry.second.first, loc);
    add(dest, code_ifthenelset{entry.second.second, std::move(restore)}, loc);
  }
}
