/// \file
/// Shell converter: eval (XCU 2.15 eval). The arguments are joined with
/// spaces and the result is run as commands in the current shell. Where
/// the text is known during conversion (literals, and variables whose
/// values are known), it is parsed and converted in place. Elsewhere, the
/// text is a dynamic site (see goto-programs/dynamic_sites.h): with a
/// resolution, each candidate text is parsed and converted in its own
/// branch; without one, a failed property reports that it is not
/// supported.

#include "shell_convert_class.h"

#include <goto-programs/dynamic_sites.h>
#include <goto-programs/goto_instruction_code.h>

#include "shfmt_parser.h"

std::optional<std::string>
shell_convertert::known_text(const shell_wordt &word) const
{
  using kindt = shell_word_partt::kindt;
  if(const auto text = static_text(word))
    return text;
  // A single unquoted expansion of a known value: its fields, which eval
  // joins with spaces.
  if(word.size() == 1)
  {
    if(const auto literal = known_literal(word.front()))
    {
      const auto ifs = known_ifs();
      if(!ifs.has_value())
        return {};
      std::string joined;
      for(const auto &field : split_known_parameter(*literal, *ifs))
        joined += (joined.empty() ? "" : " ") + field;
      if(joined.find_first_of("*?[") != std::string::npos)
        return {};
      return joined;
    }
  }
  // "..." of literal text and known values.
  std::string text;
  for(const auto &part : word)
  {
    if(part.kind != kindt::DOUBLE_QUOTED)
    {
      const auto literal = static_text({part});
      if(!literal.has_value())
        return {};
      text += *literal;
      continue;
    }
    for(const auto &inner : part.parts)
    {
      if(const auto literal = known_literal(inner))
        text += *literal;
      else if(const auto literal = static_text({inner}, true))
        text += *literal;
      else
        return {};
    }
  }
  return text;
}

void shell_convertert::convert_eval(
  const shell_commandt &command,
  code_blockt &dest)
{
  const source_locationt &loc = command.location;
  if(!command.assignments.empty())
    unsupported(loc, "variable assignment before eval");
  std::string text;
  for(std::size_t i = 1; i < command.words.size(); ++i)
  {
    const auto word = known_text(command.words[i]);
    if(!word.has_value())
    {
      convert_dynamic_eval(command, dest);
      return;
    }
    text += (text.empty() ? "" : " ") + *word;
  }

  const shell_command_listt &commands = parse_known_text(text, "eval", loc);
  set_status(dest, 0, loc);
  convert_list(commands, dest);
}

void shell_convertert::convert_dynamic_eval(
  const shell_commandt &command,
  code_blockt &dest)
{
  const source_locationt &loc = command.location;
  // The text: the arguments, each one field, joined with spaces. Where
  // the arguments cannot be expanded as one field each, there is no site.
  const irep_idt site = "shell-eval:" + id2string(loc.get_file()) + ":" +
                        id2string(loc.get_line()) + ":" +
                        id2string(loc.get_column());
  std::optional<symbol_exprt> text;
  const bool expanded =
    mark_text_sites &&
    try_convert(
      [&](code_blockt &block)
      {
        exprt joined = payload_of(expand_word(
          command.words[1], shell_word_contextt::FIELD, block, loc));
        for(std::size_t i = 2; i < command.words.size(); ++i)
        {
          joined = payload_concat(
            payload_concat(joined, payload_literal(" "), block, loc),
            payload_of(expand_word(
              command.words[i], shell_word_contextt::FIELD, block, loc)),
            block,
            loc);
        }
        text = temporary(string_type(), block, loc);
        assign(block, *text, make_value(joined), loc);
        add(
          block,
          dynamic_site_marker(
            site, *text, shell_mode(), symbol_table, loc, true),
          loc);
      },
      dest);
  const auto resolution = expanded ? resolutions.find(site) : resolutions.end();
  if(resolution == resolutions.end())
  {
    if(!expanded)
    {
      // Only the side effects of the arguments are modelled up to here.
      for(std::size_t j = 1; j < command.words.size(); ++j)
      {
        expand_word(command.words[j], shell_word_contextt::IGNORED, dest, loc);
      }
    }
    check(
      dest,
      false_exprt{},
      "shell-unsupported",
      "eval is not reached (eval of text that is not known during "
      "conversion is not supported)",
      loc);
    add(dest, code_assumet{false_exprt{}}, loc);
    return;
  }

  // The text may assign any variable.
  known_literals.clear();
  // A branch for each candidate; any other text fails the check of the
  // site.
  std::string candidates;
  for(const auto &pattern : resolution->second.templates)
  {
    candidates +=
      (candidates.empty() ? "" : ", ") +
      (pattern.size() == 1 ? pattern.front() : template_to_string(pattern));
  }
  code_blockt otherwise;
  {
    source_locationt check_loc = loc;
    check_loc.set_property_class("shell-dynamic-site");
    check_loc.set_comment(
      "the text of eval is one of the candidates of " + id2string(site) + ": " +
      (candidates.empty() ? std::string{"none"} : candidates));
    code_assertt assertion{false_exprt{}};
    assertion.add_source_location() = check_loc;
    otherwise.add(std::move(assertion));
    add(otherwise, code_assumet{false_exprt{}}, loc);
  }
  // Run \p commands in \p branch, or report that \p what is not supported.
  const auto run = [&](
                     const std::function<const shell_command_listt &()> &parse,
                     const std::string &what,
                     code_blockt &branch)
  {
    known_literals.clear();
    if(!try_convert(
         [&](code_blockt &block)
         {
           const shell_command_listt &commands = parse();
           set_status(block, 0, loc);
           convert_list(commands, block);
         },
         branch))
    {
      check(
        branch,
        false_exprt{},
        "shell-unsupported",
        "the text of eval is not " + what +
          " (eval of text that cannot be converted here is not supported)",
        loc);
      add(branch, code_assumet{false_exprt{}}, loc);
    }
  };
  // The strings first, then the templates with holes.
  std::vector<std::pair<exprt, code_blockt>> branches;
  for(const auto &candidate : resolution->second.values())
  {
    code_blockt branch;
    run(
      [&]() -> const shell_command_listt &
      { return parse_known_text(candidate, "eval", loc); },
      candidate,
      branch);
    branches.emplace_back(
      equal(*text, string_literal(candidate, loc), dest, loc),
      std::move(branch));
  }
  for(const auto &pattern : resolution->second.with_holes())
  {
    const std::string what = template_to_string(pattern);
    code_blockt branch;
    const auto code = parse_template(pattern, "eval", loc);
    if(!code.has_value())
    {
      check(
        branch,
        false_exprt{},
        "shell-unsupported",
        "the text of eval is not " + what +
          " (eval of a template with a hole that is not in a word is not "
          "supported)",
        loc);
      add(branch, code_assumet{false_exprt{}}, loc);
    }
    else
    {
      std::vector<exprt> parts;
      const exprt in_classes =
        template_parts(pattern, *code, *text, parts, branch, loc);
      check(
        branch,
        in_classes,
        "shell-unsupported",
        "the parts of the text of eval for the holes of " + what +
          " are words without blanks, quotes or other special characters "
          "(other parts are not supported)",
        loc);
      add(branch, code_assumet{in_classes}, loc);
      assign_template_parts(*code, parts, branch, loc);
      run(
        [&]() -> const shell_command_listt & { return *code->commands; },
        what,
        branch);
    }
    branches.emplace_back(
      template_matches(pattern, *text, dest, loc), std::move(branch));
  }
  for(auto it = branches.rbegin(); it != branches.rend(); ++it)
  {
    code_ifthenelset choice{
      it->first, std::move(it->second), std::move(otherwise)};
    otherwise = code_blockt{};
    add(otherwise, std::move(choice), loc);
  }
  dest.append(otherwise);
  known_literals.clear();
}

const shell_command_listt &shell_convertert::parse_known_text(
  const std::string &text,
  const std::string &what,
  const source_locationt &loc,
  const std::function<bool(shell_command_listt &)> &rewrite)
{
  // The commands of the text, which must outlive the conversion: they are
  // kept with the dot scripts.
  auto parsed = std::make_unique<shell_programt>();
  // A file of its own, so that the locations in the text (and the
  // dynamic sites in it) differ from those of the script's own commands.
  parsed->filename = id2string(loc.get_file()) + "#" + what + "-" +
                     id2string(loc.get_line()) + "-" +
                     id2string(loc.get_column());
  parsed->dialect = program.dialect;
  if(parse_shell_source(
       text, parsed->filename, *parsed, log.get_message_handler()))
  {
    unsupported(loc, what + " of text that does not parse: " + text);
  }
  if(rewrite && !rewrite(parsed->commands))
    unsupported(loc, what + " of a template with a hole that is not a word");
  // The text must not need what the conversion collected beforehand:
  // function definitions, options that set may enable, and dot scripts.
  std::map<std::string, std::vector<const shell_commandt *>> definitions;
  std::swap(definitions, function_definitions);
  collect_function_definitions(parsed->commands);
  std::swap(definitions, function_definitions);
  if(!definitions.empty())
    unsupported(loc, what + " of a function definition");
  const std::set<std::string> options_before = possible_options;
  collect_options(parsed->commands);
  if(possible_options != options_before)
    unsupported(loc, what + " of set with an option that is not set elsewhere");
  const std::size_t dots_before = dot_scripts.size();
  collect_dot_scripts(parsed->commands);
  if(dot_scripts.size() != dots_before)
    unsupported(loc, what + " of a dot script that is not run elsewhere");

  eval_programs.push_back(std::move(parsed));
  return eval_programs.back()->commands;
}
