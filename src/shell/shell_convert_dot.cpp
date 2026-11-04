/// \file
/// Shell converter: the dot special builtin (POSIX.1-2024 XCU 2.15 dot),
/// and source in bash, mksh, and zsh.
///
/// A dot script with a literal absolute path is read and parsed when the
/// script is converted, which assumes that the script will find the same
/// file when it runs. It becomes the GOTO function shell::dot::PATH, run
/// in the current environment; return leaves it.

#include "shell_convert_class.h"

#include <util/unicode.h>

#include <goto-programs/goto_instruction_code.h>

#include "shfmt_parser.h"

#include <fstream>
#include <sstream>

/// The literal path of the dot command \p command, if it has one.
std::optional<std::string>
shell_convertert::dot_script_path(const shell_commandt &command) const
{
  if(command.kind != shell_commandt::kindt::SIMPLE || command.words.size() < 2)
    return {};
  const auto name = static_text(command.words.front());
  if(!name.has_value() || !is_dot_command(*name))
    return {};
  return static_text(command.words[1]);
}

bool shell_convertert::is_dot_command(const std::string &name) const
{
  return name == "." ||
         (name == "source" && program.dialect != shell_dialectt::POSIX);
}

void shell_convertert::collect_dot_scripts(const shell_command_listt &list)
{
  for(const auto &command : list)
  {
    const auto path = dot_script_path(command);
    if(
      path.has_value() && !path->empty() && (*path)[0] == '/' &&
      dot_scripts.count(*path) == 0)
    {
      load_dot_script(*path, command.location);
    }
    for(const auto *sub :
        {&command.body, &command.condition, &command.else_body})
    {
      collect_dot_scripts(*sub);
    }
    for(const auto &item : command.case_items)
      collect_dot_scripts(item.body);
    for(const auto &word : command.words)
    {
      for(const auto &part : word)
        collect_dot_scripts(part);
    }
    for(const auto &assignment : command.assignments)
    {
      for(const auto &part : assignment.value)
        collect_dot_scripts(part);
    }
  }
}

void shell_convertert::collect_dot_scripts(const shell_word_partt &part)
{
  if(part.commands != nullptr)
    collect_dot_scripts(*part.commands);
  for(const auto &inner : part.parts)
    collect_dot_scripts(inner);
}

void shell_convertert::load_dot_script(
  const std::string &path,
  const source_locationt &loc)
{
  // Record the path first, so that a script that sources itself ends.
  auto &entry = dot_scripts[path];
  std::ifstream in{widen_if_needed(path), std::ios::binary};
  if(!in)
  {
    log.warning().source_location = loc;
    log.warning() << "dot script " << path
                  << " does not exist here; it is assumed not to exist when "
                     "the script runs"
                  << messaget::eom;
    return;
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  auto dot = std::make_unique<shell_programt>();
  dot->filename = path;
  // It runs in the same shell.
  dot->dialect = program.dialect;
  if(parse_shell_source(buffer.str(), path, *dot, log.get_message_handler()))
  {
    log.error().source_location = loc;
    log.error() << "failed to parse dot script " << path << messaget::eom;
    throw shell_conversion_errort{};
  }
  entry = std::move(dot);
  collect_dot_scripts(entry->commands);
}

std::string shell_convertert::dot_identifier(const std::string &path)
{
  return "shell::dot::" + path;
}

void shell_convertert::declare_dot_scripts()
{
  for(const auto &entry : dot_scripts)
  {
    if(entry.second == nullptr)
      continue;
    symbolt function{
      dot_identifier(entry.first), code_typet{{}, empty_typet{}}, shell_mode()};
    function.base_name = entry.first;
    function.pretty_name = entry.first;
    function.is_lvalue = true;
    function.location.set_file(entry.first);
    symbol_table.add(function);
  }
}

void shell_convertert::convert_dot_scripts()
{
  for(const auto &entry : dot_scripts)
  {
    if(entry.second == nullptr)
      continue;
    const std::string identifier = dot_identifier(entry.first);
    const std::string saved_function = current_function;
    auto saved_loops = std::move(loops);
    loops.clear();
    const bool saved_in_dot = in_dot_script;
    const bool saved_in_function = in_function;
    auto saved_literals = std::move(known_literals);
    known_literals.clear();
    in_dot_script = true;
    in_function = false;
    current_function = identifier;

    code_blockt body;
    // The status of a dot script without commands is zero.
    source_locationt loc;
    loc.set_file(entry.first);
    set_status(body, 0, loc);
    convert_list(entry.second->commands, body);

    current_function = saved_function;
    in_function = saved_in_function;
    known_literals = std::move(saved_literals);
    in_dot_script = saved_in_dot;
    loops = std::move(saved_loops);
    symbol_table.get_writeable_ref(identifier).value = std::move(body);
  }
}

void shell_convertert::convert_dot(
  const shell_commandt &command,
  code_blockt &dest)
{
  const source_locationt &loc = command.location;
  // The dot script may assign any variable.
  known_literals.clear();
  const std::string name = *static_text(command.words.front());
  if(command.words.size() < 2)
    unsupported(loc, name + " without a file");
  const auto path = dot_script_path(command);
  if(!path.has_value())
    unsupported(loc, name + " with a file name that is an expansion");
  if(path->empty() || (*path)[0] != '/')
  {
    // Found by a search of PATH, or relative to the working directory.
    unsupported(loc, name + " with a file name that is not an absolute path");
  }
  const bool has_arguments = command.words.size() > 2;
  if(has_arguments && program.dialect == shell_dialectt::POSIX)
    unsupported(loc, "arguments to .");

  const auto &dot = dot_scripts.at(*path);
  if(dot == nullptr)
  {
    // Not found: an error of a special builtin (XCU 2.8.1), but bash and
    // zsh continue.
    if(
      program.dialect == shell_dialectt::POSIX ||
      program.dialect == shell_dialectt::MKSH)
    {
      exit_shell(dest, true, loc);
    }
    else
      set_status(dest, 1, loc);
    return;
  }

  // In bash, mksh, and zsh, arguments replace the positional parameters
  // while the dot script runs.
  std::optional<std::pair<symbol_exprt, symbol_exprt>> saved;
  if(has_arguments)
  {
    const field_listt arguments =
      expand_fields(command.words.begin() + 2, command.words.end(), dest, loc);
    saved.emplace(
      temporary(field_array_type(), dest, loc),
      temporary(count_type(), dest, loc));
    assign(dest, saved->first, positional_array(), loc);
    assign(dest, saved->second, argument_count(), loc);
    set_positional_parameters(arguments, dest, loc);
  }
  add(
    dest,
    code_function_callt{
      symbol_table.lookup_ref(dot_identifier(*path)).symbol_expr()},
    loc);
  code_blockt leave;
  add(leave, code_frontend_returnt{}, loc);
  add(dest, code_ifthenelset{exited(), std::move(leave)}, loc);
  if(saved.has_value())
  {
    assign(dest, positional_array(), saved->first, loc);
    assign(dest, argument_count(), saved->second, loc);
  }
}
