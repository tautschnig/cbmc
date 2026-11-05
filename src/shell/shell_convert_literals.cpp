/// \file
/// Shell converter: the literal values of variables, as far as they are
/// known during conversion (see known_literals). Expansions of variables
/// with a known value are literals, whose field splitting, pathname
/// expansion checks, and use as command names need no solver.

#include "shell_convert_class.h"

#include <util/pointer_expr.h>
#include <util/refined_string_type.h>
#include <util/std_types.h>
#include <util/string_expr.h>

std::optional<std::string>
shell_convertert::literal_payload(const exprt &payload) const
{
  if(native())
  {
    if(payload.is_constant() && payload.type().id() == ID_string)
      return id2string(to_constant_expr(payload).get_value());
    return {};
  }
  // payload_literal: {length, &array[0]} of a constant array.
  if(payload.id() != ID_struct || payload.operands().size() != 2)
    return {};
  const exprt &pointer = payload.operands()[1];
  if(pointer.id() != ID_address_of)
    return {};
  const exprt &object = to_address_of_expr(pointer).object();
  if(object.id() != ID_index || to_index_expr(object).array().id() != ID_array)
    return {};
  std::string text;
  for(const auto &byte : to_index_expr(object).array().operands())
  {
    const auto c = numeric_cast<mp_integer>(byte);
    if(!c.has_value())
      return {};
    text += static_cast<char>(c->to_ulong());
  }
  return text;
}

std::optional<std::string>
shell_convertert::literal_of(const exprt &value) const
{
  if(value.id() != ID_struct || value.operands().size() != 4)
    return {};
  if(!value.operands()[0].is_constant() || value.operands()[0].is_zero())
    return {};
  return literal_payload(value.operands()[1]);
}

std::optional<std::string>
shell_convertert::known_literal(const shell_word_partt &part) const
{
  if(
    part.kind != shell_word_partt::kindt::PARAMETER ||
    !part.parameter_operator.empty() || part.parameter_length)
  {
    return {};
  }
  const auto found = known_literals.find(part.text);
  if(found == known_literals.end())
    return {};
  return found->second;
}

void shell_convertert::note_assignment(
  const std::string &name,
  const exprt &value)
{
  const auto literal = literal_of(value);
  if(literal.has_value())
    known_literals[name] = *literal;
  else
    known_literals.erase(name);
}

void shell_convertert::forget_assigned(const shell_commandt &command)
{
  std::set<std::string> names;
  bool any = false;
  collect_assigned({command}, names, any);
  if(any)
    known_literals.clear();
  for(const auto &name : names)
    known_literals.erase(name);
  // Whether IFS is unset is also forgotten (see shell_convert_ifs.cpp).
  if(names.count("IFS") != 0)
    forget_ifs();
}

void shell_convertert::collect_assigned(
  const shell_command_listt &list,
  std::set<std::string> &names,
  bool &any) const
{
  using kindt = shell_commandt::kindt;
  // Commands that may assign variables other than by their assignments:
  // builtins that take names, and calls of functions and dot scripts.
  static const std::set<std::string> assigning{
    "export",
    "unset",
    "readonly",
    "local",
    "declare",
    "typeset",
    "printf",
    "eval",
    ".",
    "source",
    "let",
    "mapfile",
    "readarray",
    "command"};
  for(const auto &command : list)
  {
    // Subshells, pipelines and asynchronous lists do not change the
    // shell's variables; a function definition does not run its body.
    if(
      command.background || command.kind == kindt::SUBSHELL ||
      command.kind == kindt::PIPELINE ||
      command.kind == kindt::FUNCTION_DEFINITION)
    {
      continue;
    }
    // Assignments before utilities and regular builtins only last for
    // them.
    std::optional<std::string> name;
    if(command.kind == kindt::SIMPLE && !command.words.empty())
      name = static_text(command.words.front());
    bool temporary = false;
    if(name.has_value() && function_definitions.count(*name) == 0)
    {
      const auto kind = classify(*name);
      temporary = kind == shell_command_kindt::EXTERNAL ||
                  kind == shell_command_kindt::OUTPUT ||
                  kind == shell_command_kindt::READ;
    }
    for(const auto &assignment : command.assignments)
    {
      if(!temporary)
        names.insert(assignment.name);
      collect_assigned(assignment.value, names, any);
    }
    if(command.kind == kindt::SIMPLE && !command.words.empty())
    {
      if(
        !name.has_value() || assigning.count(*name) != 0 ||
        function_definitions.count(*name) != 0)
      {
        any = true;
      }
      else if(*name == "getopts")
      {
        names.insert("OPTIND");
        names.insert("OPTARG");
        const auto target = command.words.size() > 2
                              ? static_text(command.words[2])
                              : std::nullopt;
        if(target.has_value())
          names.insert(*target);
        else
          any = true;
      }
      else if(*name == "cd")
      {
        names.insert("PWD");
        names.insert("OLDPWD");
      }
      else if(*name == "read")
      {
        // The names read assigns, if they are literal.
        const auto operands = read_operands(command);
        if(operands.has_value())
          names.insert(operands->second.begin(), operands->second.end());
        else
          any = true;
      }
    }
    if(command.kind == kindt::FOR)
      names.insert(command.name);
    for(const auto &word : command.words)
      collect_assigned(word, names, any);
    for(const auto &word : command.for_items)
      collect_assigned(word, names, any);
    collect_assigned(command.case_word, names, any);
    for(const auto &redirect : command.redirects)
      collect_assigned(redirect.target, names, any);
    for(const auto *sub :
        {&command.body, &command.condition, &command.else_body})
    {
      collect_assigned(*sub, names, any);
    }
    for(const auto &item : command.case_items)
      collect_assigned(item.body, names, any);
  }
}

void shell_convertert::collect_assigned(
  const shell_wordt &word,
  std::set<std::string> &names,
  bool &any) const
{
  using kindt = shell_word_partt::kindt;
  for(const auto &part : word)
  {
    if(part.kind == kindt::PARAMETER)
    {
      if(part.parameter_operator == "=" || part.parameter_operator == ":=")
      {
        names.insert(part.text);
      }
      collect_assigned(part.parts, names, any);
    }
    else if(part.kind == kindt::DOUBLE_QUOTED)
      collect_assigned(part.parts, names, any);
    else if(part.kind == kindt::ARITHMETIC)
    {
      // Assignments in arithmetic: =, op=, ++ and --, other than the
      // comparisons ==, !=, <= and >=.
      const std::string &t = part.text;
      for(std::size_t i = 0; i < t.size(); ++i)
      {
        const bool assignment =
          (t[i] == '=' && (i + 1 >= t.size() || t[i + 1] != '=') &&
           (i == 0 ||
            std::string{"=!<>"}.find(t[i - 1]) == std::string::npos)) ||
          t.compare(i, 2, "++") == 0 || t.compare(i, 2, "--") == 0 ||
          t.compare(i, 3, "<<=") == 0 || t.compare(i, 3, ">>=") == 0;
        if(assignment)
          any = true;
      }
    }
    // Command substitutions run in subshells.
  }
}
