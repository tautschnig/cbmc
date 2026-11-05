/// \file
/// Shell converter: functions (POSIX.1-2024 XCU 2.9.5) and return.
///
/// Each function definition in the script becomes a GOTO function
/// shell::function::NAME#K for its K-th definition in the source. As
/// functions are defined when the definition command runs, and can be
/// redefined, the variable shell::fn::NAME records which definition is
/// current (0: none). A call dispatches on it; with no definition, the
/// command is a builtin or an external utility as usual.

#include "shell_convert_class.h"

#include <goto-programs/goto_instruction_code.h>

/// Special builtins are found before functions (XCU 2.9.1.4), so the
/// application must not define functions with their names (XCU 2.9.5).
static bool is_special_builtin(const std::string &name)
{
  static const std::set<std::string> special{
    ":",
    ".",
    "break",
    "continue",
    "eval",
    "exec",
    "exit",
    "export",
    "readonly",
    "return",
    "set",
    "shift",
    "times",
    "trap",
    "unset"};
  return special.count(name) != 0;
}

void shell_convertert::collect_function_definitions(
  const shell_command_listt &list)
{
  for(const auto &command : list)
  {
    if(command.kind == shell_commandt::kindt::FUNCTION_DEFINITION)
    {
      auto &definitions = function_definitions[command.name];
      definitions.push_back(&command);
    }
    for(const auto *sub :
        {&command.body, &command.condition, &command.else_body})
    {
      collect_function_definitions(*sub);
    }
    for(const auto &item : command.case_items)
      collect_function_definitions(item.body);
    for(const auto &word : command.words)
      collect_function_definitions(word);
    for(const auto &assignment : command.assignments)
      collect_function_definitions(assignment.value);
    for(const auto &word : command.for_items)
      collect_function_definitions(word);
    collect_function_definitions(command.case_word);
    for(const auto &redirect : command.redirects)
    {
      collect_function_definitions(redirect.target);
      collect_function_definitions(redirect.here_document);
    }
  }
}

void shell_convertert::collect_function_definitions(
  const std::vector<shell_word_partt> &parts)
{
  for(const auto &part : parts)
  {
    if(part.commands != nullptr)
      collect_function_definitions(*part.commands);
    collect_function_definitions(part.parts);
  }
}

std::string
shell_convertert::function_identifier(const std::string &name, std::size_t k)
{
  return "shell::function::" + name + "#" + std::to_string(k);
}

symbol_exprt shell_convertert::current_definition(const std::string &name)
{
  return global_symbol("shell::fn::" + name, name, current_definition_type());
}

/// The number of \p definition among those of its name, from 1.
std::size_t
shell_convertert::definition_number(const shell_commandt &definition) const
{
  const auto &definitions = function_definitions.at(definition.name);
  for(std::size_t k = 0; k < definitions.size(); ++k)
  {
    if(definitions[k] == &definition)
      return k + 1;
  }
  UNREACHABLE;
}

void shell_convertert::convert_function_definition(
  const shell_commandt &command,
  code_blockt &dest)
{
  const source_locationt &loc = command.location;
  if(is_special_builtin(command.name))
  {
    unsupported(
      loc, "function named like the special builtin '" + command.name + "'");
  }
  assign(
    dest,
    current_definition(command.name),
    from_integer(definition_number(command), current_definition_type()),
    loc);
  set_status(dest, 0, loc);
}

/// Convert the body of \p definition into its GOTO function.
void shell_convertert::convert_function_body(const shell_commandt &definition)
{
  const std::string identifier =
    function_identifier(definition.name, definition_number(definition));

  // A function body is converted on its own: loops of the caller are not
  // visible (break and continue cannot leave the function).
  const std::string saved_function = current_function;
  auto saved_loops = std::move(loops);
  loops.clear();
  current_function = identifier;
  const bool saved_in_function = in_function;
  in_function = true;
  // Nothing is known about the variables where it is called.
  auto saved_literals = std::move(known_literals);
  known_literals.clear();

  // The variables that the body makes local are restored at its end,
  // which its returns jump to.
  auto saved_locals = std::move(function_locals);
  const irep_idt saved_end = function_end_label;
  function_end_label = new_label("function_end");
  code_blockt body;
  save_locals(definition.body, body, definition.location);
  convert_list(definition.body, body);
  label(body, function_end_label);
  restore_locals(body, definition.location);
  function_locals = std::move(saved_locals);
  function_end_label = saved_end;

  known_literals = std::move(saved_literals);
  in_function = saved_in_function;
  current_function = saved_function;
  loops = std::move(saved_loops);

  symbol_table.get_writeable_ref(identifier).value = std::move(body);
}

/// Declare the functions before their bodies are converted, as calls can
/// come first.
void shell_convertert::declare_functions()
{
  for(const auto &entry : function_definitions)
  {
    for(std::size_t k = 1; k <= entry.second.size(); ++k)
    {
      symbolt function{
        function_identifier(entry.first, k),
        code_typet{{}, empty_typet{}},
        shell_mode()};
      function.base_name = entry.first;
      function.pretty_name = entry.first;
      function.is_lvalue = true;
      function.location = entry.second[k - 1]->location;
      symbol_table.add(function);
    }
  }
}

void shell_convertert::convert_function_bodies()
{
  for(const auto &entry : function_definitions)
  {
    for(const shell_commandt *definition : entry.second)
      convert_function_body(*definition);
  }
}

void shell_convertert::convert_function_call(
  const shell_commandt &command,
  code_blockt &dest)
{
  const source_locationt &loc = command.location;
  const std::string name = *static_text(command.words.front());
  // The function's positional parameters are its arguments (XCU 2.9.5).
  const field_listt arguments =
    expand_fields(command.words.begin() + 1, command.words.end(), dest, loc);

  // Assignments before the call: whether they persist after it is
  // unspecified (XCU 2.9.1.2), but dash, bash, busybox, mksh and zsh all
  // restore the previous values, also where the function assigned them.
  // They are made after the arguments are expanded.
  std::vector<std::pair<symbol_exprt, symbol_exprt>> saved_variables;
  if(!command.assignments.empty())
  {
    bool words = false;
    for(std::size_t i = 1; i < command.words.size(); ++i)
      words |= has_side_effects(command.words[i]);
    for(const auto &assignment : command.assignments)
    {
      if(words && has_side_effects(assignment.value))
      {
        unsupported(
          loc,
          "expansions with side effects in both the arguments and the "
          "assignments of a function call");
      }
      if(
        assignment.name == "IFS" || assignment.name == "_" ||
        assignment.name == "RANDOM")
      {
        unsupported(
          loc, "assignment of " + assignment.name + " before a function call");
      }
      const symbol_exprt var = variable(assignment.name, loc);
      const symbol_exprt saved = temporary(string_type(), dest, loc);
      assign(dest, saved, var, loc);
      saved_variables.emplace_back(var, saved);
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
  const symbol_exprt saved_array = temporary(field_array_type(), dest, loc);
  const symbol_exprt saved_count = temporary(count_type(), dest, loc);
  assign(dest, saved_array, positional_array(), loc);
  assign(dest, saved_count, argument_count(), loc);
  set_positional_parameters(arguments, dest, loc);

  // -e stays ignored in the function if it is ignored at the call.
  const symbol_exprt saved_ignored = temporary(bool_typet{}, dest, loc);
  if(errexit_ignored && option_possible("errexit"))
  {
    assign(dest, saved_ignored, errexit_ignored_in_caller(), loc);
    assign(dest, errexit_ignored_in_caller(), true_exprt{}, loc);
  }

  // The function may assign any variable.
  known_literals.clear();
  const auto &definitions = function_definitions.at(name);
  for(std::size_t k = 1; k <= definitions.size(); ++k)
  {
    code_blockt call;
    code_function_callt function_call{
      symbol_table.lookup_ref(function_identifier(name, k)).symbol_expr()};
    add(call, function_call, loc);
    add(
      dest,
      code_ifthenelset{
        equal_exprt{
          current_definition(name), from_integer(k, current_definition_type())},
        std::move(call)},
      loc);
  }

  // After exit in the function, return from this function too.
  code_blockt leave;
  add(leave, code_frontend_returnt{}, loc);
  add(dest, code_ifthenelset{exited(), std::move(leave)}, loc);

  assign(dest, positional_array(), saved_array, loc);
  assign(dest, argument_count(), saved_count, loc);
  if(errexit_ignored && option_possible("errexit"))
    assign(dest, errexit_ignored_in_caller(), saved_ignored, loc);
  for(const auto &entry : saved_variables)
    assign(dest, entry.first, entry.second, loc);
}

/// return [n] (XCU 2.15 return).
void shell_convertert::convert_return(
  const shell_commandt &command,
  code_blockt &dest)
{
  const source_locationt &loc = command.location;
  if(command.words.size() > 2)
    unsupported(loc, "return with several operands");
  if(!in_function && !in_dot_script)
  {
    if(program.dialect == shell_dialectt::BASH)
    {
      // bash reports an error and continues.
      for(std::size_t i = 1; i < command.words.size(); ++i)
        expand_word(command.words[i], shell_word_contextt::IGNORED, dest, loc);
      nondet_status(dest, 255, loc);
      add(
        dest,
        code_assumet{notequal_exprt{status(), from_integer(0, status_type())}},
        loc);
      return;
    }
    // dash, busybox, mksh and zsh exit, as with exit; bash as sh exits
    // with status 2.
    if(command.words.size() == 2)
      status_from_operand(command.words[1], "return", true, dest, loc);
    if(program.dialect == shell_dialectt::POSIX)
    {
      assign(
        dest,
        status(),
        if_exprt{
          nondet(bool_typet{}, loc), status(), from_integer(2, status_type())},
        loc);
    }
    exit_shell(dest, false, loc);
    return;
  }
  if(command.words.size() == 2)
    status_from_operand(command.words[1], "return", false, dest, loc);
  if(in_function && !function_end_label.empty())
    goto_label(dest, function_end_label);
  else
    add(dest, code_frontend_returnt{}, loc);
}

/// Set the status to the operand of exit or return: an unsigned decimal
/// integer. exit uses the value modulo 256 in all shells; for return,
/// values above 255 are kept by dash, zsh, and yash.
void shell_convertert::status_from_operand(
  const shell_wordt &word,
  const std::string &builtin,
  bool modulo,
  code_blockt &dest,
  const source_locationt &loc)
{
  const auto text = static_text(word);
  if(
    text.has_value() && !text->empty() && text->size() < 10 &&
    text->find_first_not_of("0123456789") == std::string::npos)
  {
    const std::size_t value = std::stoul(*text);
    if(value > 255 && !modulo)
      unsupported(loc, builtin + " with a value above 255");
    set_status(dest, static_cast<unsigned>(value % 256), loc);
    return;
  }

  const exprt string = expand_word(word, shell_word_contextt::FIELD, dest, loc);
  const symbol_exprt value = temporary(integer_type(), dest, loc);
  const exprt valid =
    test_integer(string, trailing_blankst::NONE, value, dest, loc);
  check(
    dest,
    and_exprt{
      valid,
      binary_relation_exprt{value, ID_ge, from_integer(0, integer_type())},
      binary_relation_exprt{
        value, ID_le, from_integer(modulo ? 2147483647 : 255, integer_type())}},
    "shell-unspecified",
    "operand of " + builtin + " is a decimal integer in [0, " +
      (modulo ? "2147483647" : "255") + "]",
    loc);
  assign(
    dest,
    status(),
    typecast_exprt{
      mod_exprt{value, from_integer(256, integer_type())}, status_type()},
    loc);
}
