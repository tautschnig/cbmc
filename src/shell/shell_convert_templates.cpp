/// \file
/// Shell converter: candidates of dynamic sites that are templates with
/// holes (see goto-programs/dynamic_sites.h), for text that becomes code
/// (eval, and the action of trap).
///
/// The text of a template is parsed once, with a placeholder for each
/// hole. Where a hole is (part of) a word, it becomes a quoted reference to
/// a ghost variable, which holds the part of the value for that hole. This
/// is exact if the part has no character that its position would give a
/// meaning: that is its class, which a property checks:
///
/// - unquoted: a non-empty string of letters, digits and _ . / : + @ % = -
///   (no blanks, quotes, expansions, operators, pattern or brace
///   characters, and no tilde);
/// - in double quotes: no ", \, $ or `;
/// - in single quotes (or $'...'): no ' or \.
///
/// A hole anywhere else (a command name, a variable name, an arithmetic
/// expression, a here-document) makes the template unusable here.

#include "shell_convert_class.h"

#include <bitset>
#include <cctype>

namespace
{
/// Rewrites the commands of a template, replacing each placeholder by a
/// quoted reference to its ghost variable.
class hole_rewritert
{
public:
  using classt = shell_convertert::hole_classt;

  hole_rewritert(
    const std::vector<std::string> &placeholders,
    const std::vector<std::string> &ghosts)
    : placeholders(placeholders), ghosts(ghosts), classes(placeholders.size())
  {
  }

  const std::vector<std::string> &placeholders;
  const std::vector<std::string> &ghosts;
  std::vector<std::optional<classt>> classes;
  bool failed = false;

  bool mentions(const std::string &text) const
  {
    for(const auto &placeholder : placeholders)
    {
      if(text.find(placeholder) != std::string::npos)
        return true;
    }
    return false;
  }

  bool mentions(const std::vector<shell_word_partt> &parts) const
  {
    for(const auto &part : parts)
    {
      if(
        mentions(part.text) || mentions(part.parts) || mentions(part.index) ||
        mentions(part.replacement) ||
        (part.commands != nullptr && mentions(*part.commands)))
      {
        return true;
      }
    }
    return false;
  }

  bool mentions(const shell_command_listt &list) const
  {
    // Rewriting a copy finds the placeholders.
    hole_rewritert copy{placeholders, ghosts};
    shell_command_listt rewritten = list;
    copy.rewrite(rewritten);
    for(const auto &found : copy.classes)
    {
      if(found.has_value())
        return true;
    }
    return copy.failed;
  }

  void found(std::size_t index, classt hole_class)
  {
    if(classes[index].has_value() && *classes[index] != hole_class)
      failed = true;
    classes[index] = hole_class;
  }

  /// The first placeholder in \p text at or after \p from: its index and
  /// position.
  std::optional<std::pair<std::size_t, std::size_t>>
  next(const std::string &text, std::size_t from) const
  {
    std::optional<std::pair<std::size_t, std::size_t>> result;
    for(std::size_t i = 0; i < placeholders.size(); ++i)
    {
      const std::size_t at = text.find(placeholders[i], from);
      if(
        at != std::string::npos && (!result.has_value() || at < result->second))
        result = std::make_pair(i, at);
    }
    return result;
  }

  void rewrite(std::vector<shell_word_partt> &parts, bool in_double_quotes)
  {
    using kindt = shell_word_partt::kindt;
    std::vector<shell_word_partt> result;
    for(auto &part : parts)
    {
      if(part.kind == kindt::LITERAL || part.kind == kindt::QUOTED_LITERAL)
      {
        std::size_t from = 0;
        while(const auto hole = next(part.text, from))
        {
          const std::string before =
            part.text.substr(from, hole->second - from);
          // A tilde prefix ends at a quote, which would change it.
          if(
            part.kind == kindt::LITERAL && !in_double_quotes &&
            part.text.substr(0, hole->second).find('~') != std::string::npos)
          {
            failed = true;
          }
          // A backslash before the hole would quote its first character
          // rather than the placeholder's.
          if(
            !part.text.substr(0, hole->second).empty() &&
            part.text[hole->second - 1] == '\\')
          {
            failed = true;
          }
          if(!before.empty())
          {
            shell_word_partt text = part;
            text.text = before;
            result.push_back(std::move(text));
          }
          shell_word_partt reference;
          reference.kind = kindt::PARAMETER;
          reference.text = ghosts[hole->first];
          reference.location = part.location;
          if(in_double_quotes)
            result.push_back(std::move(reference));
          else
          {
            shell_word_partt quoted;
            quoted.kind = kindt::DOUBLE_QUOTED;
            quoted.location = part.location;
            quoted.parts.push_back(std::move(reference));
            result.push_back(std::move(quoted));
          }
          found(
            hole->first,
            part.kind == kindt::QUOTED_LITERAL ? classt::SINGLE_QUOTED
            : in_double_quotes                 ? classt::DOUBLE_QUOTED
                                               : classt::UNQUOTED);
          from = hole->second + placeholders[hole->first].size();
        }
        if(from == 0)
          result.push_back(std::move(part));
        else if(from < part.text.size())
        {
          part.text = part.text.substr(from);
          result.push_back(std::move(part));
        }
        continue;
      }
      if(part.kind == kindt::DOUBLE_QUOTED)
        rewrite(part.parts, true);
      else if(part.kind == kindt::PARAMETER)
      {
        // A hole in the name of a variable, or in an index, is not a word.
        if(mentions(part.text) || mentions(part.index))
          failed = true;
        rewrite(part.parts, in_double_quotes);
        rewrite(part.replacement, in_double_quotes);
      }
      else if(part.kind == kindt::COMMAND_SUBSTITUTION)
      {
        if(part.commands != nullptr)
        {
          auto commands = std::make_shared<shell_command_listt>(*part.commands);
          rewrite(*commands);
          part.commands = std::move(commands);
        }
      }
      else if(part.kind == kindt::ARITHMETIC && mentions(part.text))
        failed = true;
      result.push_back(std::move(part));
    }
    parts = std::move(result);
  }

  void rewrite(shell_testt &test)
  {
    rewrite(test.word, false);
    rewrite(test.right, false);
    for(auto &operand : test.operands)
      rewrite(operand);
  }

  void rewrite(shell_command_listt &list)
  {
    for(auto &command : list)
      rewrite(command);
  }

  void rewrite(shell_commandt &command)
  {
    if(mentions(command.name))
      failed = true;
    for(auto &assignment : command.assignments)
    {
      if(mentions(assignment.name) || mentions(assignment.index))
        failed = true;
      rewrite(assignment.value, false);
      for(auto &element : assignment.elements)
        rewrite(element, false);
    }
    for(std::size_t i = 0; i < command.words.size(); ++i)
    {
      // A hole in the command name is not supported here.
      if(i == 0 && mentions(command.words[i]))
        failed = true;
      rewrite(command.words[i], false);
    }
    for(auto &test : command.test)
      rewrite(test);
    for(auto &redirect : command.redirects)
    {
      if(mentions(redirect.here_document))
        failed = true;
      rewrite(redirect.target, false);
    }
    rewrite(command.body);
    rewrite(command.condition);
    rewrite(command.else_body);
    for(auto &item : command.for_items)
      rewrite(item, false);
    rewrite(command.case_word, false);
    for(auto &item : command.case_items)
    {
      for(auto &pattern : item.patterns)
        rewrite(pattern, false);
      rewrite(item.body);
    }
  }
};
} // namespace

std::optional<shell_convertert::template_codet>
shell_convertert::parse_template(
  const dynamic_templatet &pattern,
  const std::string &what,
  const source_locationt &loc)
{
  template_codet code;
  std::vector<std::string> placeholders;
  std::string text;
  for(std::size_t i = 0; i < pattern.size(); ++i)
  {
    text += pattern[i];
    if(i + 1 == pattern.size())
      break;
    placeholders.push_back("CPROVERHOLE" + std::to_string(i) + "X");
    code.ghosts.push_back("__cprover_hole_" + std::to_string(holes_so_far++));
    text += placeholders.back();
  }
  for(const auto &part : pattern)
  {
    // A literal part that looks like a placeholder would be taken for one.
    if(part.find("CPROVERHOLE") != std::string::npos)
      return {};
  }
  code.text = text;
  for(std::size_t i = 0; i < placeholders.size(); ++i)
  {
    const std::string &placeholder = placeholders[i];
    code.text.replace(
      code.text.find(placeholder), placeholder.size(), "$" + code.ghosts[i]);
  }
  code.classes.resize(placeholders.size());
  bool usable = true;
  code_blockt unused;
  if(!try_convert(
       [&](code_blockt &)
       {
         code.commands = &parse_known_text(
           text,
           what,
           loc,
           [&](shell_command_listt &commands)
           {
             hole_rewritert rewrite{placeholders, code.ghosts};
             rewrite.rewrite(commands);
             for(std::size_t i = 0; i < placeholders.size(); ++i)
             {
               if(!rewrite.classes[i].has_value())
                 return false;
               code.classes[i] = *rewrite.classes[i];
             }
             return !rewrite.failed;
           });
       },
       unused))
  {
    usable = false;
  }
  if(!usable)
    return {};
  return code;
}

exprt shell_convertert::template_parts(
  const dynamic_templatet &pattern,
  const template_codet &code,
  const exprt &value,
  std::vector<exprt> &parts,
  code_blockt &dest,
  const source_locationt &loc)
{
  const exprt v = payload_of(value);
  const exprt length = payload_length(v, dest, loc);
  const exprt zero = from_integer(0, integer_type());
  const auto size = [this](const std::string &text) -> exprt
  { return from_integer(text.size(), integer_type()); };
  exprt::operandst conditions;
  // The first part is a prefix.
  if(!pattern.front().empty())
  {
    conditions.push_back(equal_exprt{
      payload_index_of(v, payload_literal(pattern.front()), zero, dest, loc),
      zero});
  }
  exprt start = size(pattern.front());
  // Each inner part at its first occurrence after the previous one.
  for(std::size_t i = 1; i + 1 < pattern.size(); ++i)
  {
    const exprt at =
      payload_index_of(v, payload_literal(pattern[i]), start, dest, loc);
    conditions.push_back(binary_relation_exprt{at, ID_ge, zero});
    parts.push_back(payload_substring(v, start, at, dest, loc));
    start = plus_exprt{at, size(pattern[i])};
  }
  // The last part is a suffix after them.
  const exprt end = minus_exprt{length, size(pattern.back())};
  conditions.push_back(binary_relation_exprt{end, ID_ge, start});
  if(!pattern.back().empty())
  {
    conditions.push_back(equal_exprt{
      payload_substring(v, end, length, dest, loc),
      payload_literal(pattern.back())});
  }
  parts.push_back(payload_substring(v, start, end, dest, loc));

  // Each part in its class.
  for(std::size_t i = 0; i < parts.size(); ++i)
  {
    std::bitset<256> forbidden;
    switch(code.classes[i])
    {
    case hole_classt::UNQUOTED:
      forbidden.set();
      for(int c = 0; c < 256; ++c)
      {
        if(
          std::isalnum(c) ||
          std::string{"_./:+@%=-"}.find(char(c)) != std::string::npos)
        {
          forbidden.reset(c);
        }
      }
      conditions.push_back(binary_relation_exprt{
        payload_length(parts[i], dest, loc), ID_gt, zero});
      break;
    case hole_classt::DOUBLE_QUOTED:
      for(const unsigned char c : {'"', '\\', '$', '`'})
        forbidden.set(c);
      break;
    case hole_classt::SINGLE_QUOTED:
      for(const unsigned char c : {'\'', '\\'})
        forbidden.set(c);
      break;
    }
    conditions.push_back(
      not_exprt{payload_contains_byte(parts[i], forbidden, dest, loc)});
  }
  return conjunction(conditions);
}

void shell_convertert::assign_template_parts(
  const template_codet &code,
  const std::vector<exprt> &parts,
  code_blockt &dest,
  const source_locationt &loc)
{
  for(std::size_t i = 0; i < parts.size(); ++i)
    assign_variable(code.ghosts[i], make_value(parts[i]), dest, loc);
}
