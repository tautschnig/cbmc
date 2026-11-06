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

#include <goto-programs/dynamic_sites.h>
#include <goto-programs/goto_instruction_code.h>

#include "shfmt_parser.h"

#include <fstream>
#include <sstream>

/// The paths that the file name of the dot command \p command may be,
/// optimistically: each variable may have any literal value that it is
/// assigned anywhere (see dot_candidates). Loading a script that does not
/// run is harmless; convert_dot uses the value that is known where the
/// command runs.
std::vector<std::string>
shell_convertert::dot_script_paths(const shell_commandt &command) const
{
  if(command.kind != shell_commandt::kindt::SIMPLE || command.words.size() < 2)
    return {};
  const auto name = static_text(command.words.front());
  if(!name.has_value() || !is_dot_command(*name))
    return {};
  std::vector<std::string> paths{""};
  const std::function<bool(const shell_wordt &)> append =
    [&](const shell_wordt &word)
  {
    using kindt = shell_word_partt::kindt;
    for(const auto &part : word)
    {
      std::vector<std::string> texts;
      if(part.kind == kindt::DOUBLE_QUOTED)
      {
        if(!append(part.parts))
          return false;
        continue;
      }
      if(
        part.kind == kindt::PARAMETER && part.parameter_operator.empty() &&
        !part.parameter_length)
      {
        const auto found = dot_candidates.find(part.text);
        if(found == dot_candidates.end())
          return false;
        texts.assign(found->second.begin(), found->second.end());
      }
      else if(const auto text = static_text({part}, true))
        texts.push_back(*text);
      else
        return false;
      std::vector<std::string> next;
      for(const auto &path : paths)
      {
        for(const auto &text : texts)
          next.push_back(path + text);
      }
      if(next.size() > 8)
        return false;
      paths = std::move(next);
    }
    return true;
  };
  if(!append(command.words[1]))
    return {};
  return paths;
}

void shell_convertert::collect_dot_candidates(const shell_command_listt &list)
{
  for(const auto &command : list)
  {
    for(const auto &assignment : command.assignments)
    {
      if(const auto value = static_text(assignment.value))
        dot_candidates[assignment.name].insert(*value);
    }
    for(const auto *sub :
        {&command.body, &command.condition, &command.else_body})
    {
      collect_dot_candidates(*sub);
    }
    for(const auto &item : command.case_items)
      collect_dot_candidates(item.body);
  }
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
    std::vector<std::string> paths = dot_script_paths(command);
    // The candidates of the dynamic site of the file name, if resolved. The
    // identifier is only made for dot commands: interning more identifiers
    // changes their order, to which the string solver is sensitive.
    const auto name = command.kind == shell_commandt::kindt::SIMPLE &&
                          command.words.size() >= 2 && !resolutions.empty()
                        ? static_text(command.words.front())
                        : std::nullopt;
    const auto resolution = name.has_value() && is_dot_command(*name)
                              ? resolutions.find(dot_site(command.location))
                              : resolutions.end();
    if(resolution != resolutions.end())
    {
      paths.insert(
        paths.end(),
        resolution->second.values.begin(),
        resolution->second.values.end());
    }
    for(const auto &path : paths)
    {
      if(!path.empty() && path[0] == '/' && dot_scripts.count(path) == 0)
        load_dot_script(path, command.location);
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
  collect_dot_candidates(entry->commands);
  collect_dot_scripts(entry->commands);
}

irep_idt shell_convertert::dot_site(const source_locationt &loc)
{
  return "shell-dot:" + id2string(loc.get_file()) + ":" +
         id2string(loc.get_line()) + ":" + id2string(loc.get_column());
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
    auto saved_locals = std::move(function_locals);
    function_locals.reset();
    const irep_idt saved_end = function_end_label;
    function_end_label.clear();
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
    function_end_label = saved_end;
    function_locals = std::move(saved_locals);
    loops = std::move(saved_loops);
    symbol_table.get_writeable_ref(identifier).value = std::move(body);
  }
}

void shell_convertert::convert_dot(
  const shell_commandt &command,
  code_blockt &dest)
{
  const source_locationt &loc = command.location;
  const std::string name = *static_text(command.words.front());
  if(command.words.size() < 2)
    unsupported(loc, name + " without a file");
  const bool has_arguments = command.words.size() > 2;
  if(has_arguments && program.dialect == shell_dialectt::POSIX)
    unsupported(loc, "arguments to .");
  const auto path = known_text(command.words[1]);
  if(
    path.has_value() && !path->empty() && (*path)[0] == '/' &&
    dot_scripts.count(*path) != 0)
  {
    convert_dot_script(command, *path, dest);
    return;
  }

  // A file whose name is not known here, or that is found by a search of
  // PATH or relative to the working directory: a dynamic site (see
  // goto-programs/dynamic_sites.h), whose candidates with an absolute path
  // that were read are run.
  const irep_idt site = dot_site(loc);
  const auto resolution = resolutions.find(site);
  const bool resolved = resolution != resolutions.end() &&
                        !resolution->second.containing.has_value();
  known_literals.clear();
  std::optional<symbol_exprt> file;
  if(!path.has_value() && mark_text_sites)
  {
    file = temporary(string_type(), dest, loc);
    assign(
      dest,
      *file,
      expand_word(command.words[1], shell_word_contextt::FIELD, dest, loc),
      loc);
    add(
      dest,
      dynamic_site_marker(site, *file, shell_mode(), symbol_table, loc),
      loc);
  }
  if(!resolved || !file.has_value())
  {
    for(std::size_t i = 1; i < command.words.size(); ++i)
    {
      if(i != 1 || !file.has_value())
        expand_word(command.words[i], shell_word_contextt::IGNORED, dest, loc);
    }
    check(
      dest,
      false_exprt{},
      "shell-unsupported",
      name + " " + display(command.words[1]) +
        " is not reached (a dot script whose absolute path is not known "
        "here is not supported)",
      loc);
    add(dest, code_assumet{false_exprt{}}, loc);
    return;
  }

  // With a resolution: a branch for each candidate; any other value fails
  // the check of the site.
  std::string candidates;
  for(const auto &text : resolution->second.values)
    candidates += (candidates.empty() ? "" : ", ") + text;
  code_blockt otherwise;
  {
    source_locationt check_loc = loc;
    check_loc.set_property_class("shell-dynamic-site");
    check_loc.set_comment(
      "the file " + display(command.words[1]) + " of " + name +
      " is one of the candidates of " + id2string(site) + ": " +
      (candidates.empty() ? std::string{"none"} : candidates));
    code_assertt assertion{false_exprt{}};
    assertion.add_source_location() = check_loc;
    otherwise.add(std::move(assertion));
    add(otherwise, code_assumet{false_exprt{}}, loc);
  }
  shell_commandt copy = command;
  for(auto it = resolution->second.values.rbegin();
      it != resolution->second.values.rend();
      ++it)
  {
    const std::string &text = *it;
    code_blockt branch;
    if(!text.empty() && text[0] == '/' && dot_scripts.count(text) != 0)
    {
      shell_word_partt part;
      part.kind = shell_word_partt::kindt::QUOTED_LITERAL;
      part.text = text;
      part.location = command.words[1].front().location;
      copy.words[1] = {part};
      convert_dot_script(copy, text, branch);
    }
    else
    {
      check(
        branch,
        false_exprt{},
        "shell-unsupported",
        name + " " + display(command.words[1]) + " is not " + text +
          " (a dot script whose absolute path is not known here is not "
          "supported)",
        loc);
      add(branch, code_assumet{false_exprt{}}, loc);
    }
    code_ifthenelset choice{
      equal(*file, string_literal(text, loc), dest, loc),
      std::move(branch),
      std::move(otherwise)};
    otherwise = code_blockt{};
    add(otherwise, std::move(choice), loc);
  }
  dest.append(otherwise);
  known_literals.clear();
}

void shell_convertert::convert_dot_script(
  const shell_commandt &command,
  const std::string &path,
  code_blockt &dest)
{
  const source_locationt &loc = command.location;
  const bool has_arguments = command.words.size() > 2;
  // The dot script may assign any variable.
  known_literals.clear();
  const auto &dot = dot_scripts.at(path);
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
      symbol_table.lookup_ref(dot_identifier(path)).symbol_expr()},
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
