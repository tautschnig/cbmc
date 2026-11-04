/// \file
/// Shell converter: eval (XCU 2.15 eval). The arguments are joined with
/// spaces and the result is run as commands in the current shell. Where
/// the text is known during conversion (literals, and variables whose
/// values are known), it is parsed and converted in place; elsewhere, a
/// failed property reports that it is not supported.

#include "shell_convert_class.h"

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
      for(const auto &field :
          split_literal(*literal, *ifs, program.dialect == shell_dialectt::ZSH))
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
      // Only the side effects of the arguments are modelled up to here.
      for(std::size_t j = 1; j < command.words.size(); ++j)
      {
        expand_word(command.words[j], shell_word_contextt::IGNORED, dest, loc);
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
    text += (text.empty() ? "" : " ") + *word;
  }

  // The commands of the text, which must outlive the conversion: they are
  // kept with the dot scripts.
  auto parsed = std::make_unique<shell_programt>();
  parsed->filename = id2string(loc.get_file());
  parsed->dialect = program.dialect;
  if(parse_shell_source(
       text, parsed->filename, *parsed, log.get_message_handler()))
  {
    unsupported(loc, "eval of text that does not parse: " + text);
  }
  // The text must not need what the conversion collected beforehand:
  // function definitions, options that set may enable, and dot scripts.
  std::map<std::string, std::vector<const shell_commandt *>> definitions;
  std::swap(definitions, function_definitions);
  collect_function_definitions(parsed->commands);
  std::swap(definitions, function_definitions);
  if(!definitions.empty())
    unsupported(loc, "eval of a function definition");
  const std::set<std::string> options_before = possible_options;
  collect_options(parsed->commands);
  if(possible_options != options_before)
    unsupported(loc, "eval of set with an option that is not set elsewhere");
  const std::size_t dots_before = dot_scripts.size();
  collect_dot_scripts(parsed->commands);
  if(dot_scripts.size() != dots_before)
    unsupported(loc, "eval of a dot script that is not run elsewhere");

  set_status(dest, 0, loc);
  convert_list(parsed->commands, dest);
  eval_programs.push_back(std::move(parsed));
}
