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
/// - unquoted in a word that is split into fields (a command's words, and
///   the words of for): also spaces and tabs, possibly empty, as the
///   reference is then unquoted, so that field splitting with the default
///   IFS makes the same words as parsing the text;
/// - in double quotes: no ", \, $ or `;
/// - in single quotes (or $'...'): no ' or \, except, with native strings,
///   in '\'' (which ends the quotes, adds a quoted ', and starts them
///   again, as quoting a value for the shell does); the ghost variable then
///   holds the part with each '\'' replaced by '.
///
/// A hole in a command name makes it a dynamic command name (see
/// convert_dynamic_simple). A hole in the name of a parameter expansion, or
/// of an assignment without a command name, is an indirect variable: its
/// part is a name (letters, digits and _, not starting with a digit at the
/// start of the name), and the expansion or assignment dispatches over the
/// script's variables whose names match (see expand_indirect_parameter). A
/// hole anywhere else (an arithmetic expression, a here-document) makes the
/// template unusable here.

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

  void rewrite(
    std::vector<shell_word_partt> &parts,
    bool in_double_quotes,
    bool fields = false)
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
          const bool split =
            fields && !in_double_quotes && part.kind == kindt::LITERAL;
          if(in_double_quotes || split)
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
            : split                            ? classt::FIELDS
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
        // A hole in the name of a variable is an indirect variable; one in
        // an index is not supported.
        name_hole(part.text);
        if(mentions(part.index))
          failed = true;
        rewrite(part.parts, in_double_quotes, fields);
        rewrite(part.replacement, in_double_quotes, fields);
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

  /// Record the hole in the name \p name, if it has one.
  void name_hole(const std::string &name)
  {
    const auto hole = next(name, 0);
    if(!hole.has_value())
      return;
    // One hole per name.
    if(next(name, hole->second + 1).has_value())
      failed = true;
    found(hole->first, hole->second == 0 ? classt::NAME_START : classt::NAME);
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
      // An indirect assignment, only without a command name.
      if(mentions(assignment.name) && !command.words.empty())
        failed = true;
      name_hole(assignment.name);
      if(mentions(assignment.index))
        failed = true;
      rewrite(assignment.value, false);
      for(auto &element : assignment.elements)
        rewrite(element, false);
    }
    // A hole in the command name makes it a dynamic command name. The
    // words are split into fields, except the arguments of declaration
    // utilities, which some shells expand as assignments (XCU 2.9.1.1).
    static const std::set<std::string> declaration{
      "export", "readonly", "local", "declare", "typeset"};
    bool declares = false;
    if(!command.words.empty() && command.words.front().size() == 1)
    {
      const auto &front = command.words.front().front();
      declares = front.kind == shell_word_partt::kindt::LITERAL &&
                 declaration.count(front.text) != 0;
    }
    for(std::size_t i = 0; i < command.words.size(); ++i)
      rewrite(command.words[i], false, i == 0 || !declares);
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
      rewrite(item, false, true);
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
    const std::string number = std::to_string(holes_so_far++);
    placeholders.push_back("CPROVERHOLE" + number + "X");
    code.ghosts.push_back("__cprover_hole_" + number);
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
             if(rewrite.failed)
               return false;
             for(std::size_t i = 0; i < placeholders.size(); ++i)
             {
               if(
                 code.classes[i] == hole_classt::NAME ||
                 code.classes[i] == hole_classt::NAME_START)
               {
                 name_holes[placeholders[i]] = code.ghosts[i];
               }
             }
             return true;
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

exprt shell_convertert::template_decomposition(
  const dynamic_templatet &pattern,
  const exprt &value,
  template_partst &parts,
  code_blockt &dest,
  const source_locationt &loc)
{
  // For refined strings: a prefix test, the first occurrence of each inner
  // part, and the comparison of the last bytes with the last part. Other
  // encodings were slower: substring comparisons for the prefix, the
  // suffix test, rebuilding the value by concatenation, and index_of for
  // the prefix (the first took 161 s on a minimal eval, rather than 1 s).
  const exprt v = payload_of(value);
  parts.value = v;
  const exprt length = payload_length(v, dest, loc);
  const auto size = [this](const std::string &text) -> exprt
  { return from_integer(text.size(), integer_type()); };
  exprt::operandst conditions;
  if(!pattern.front().empty())
  {
    conditions.push_back(
      payload_has_prefix(v, payload_literal(pattern.front()), dest, loc));
  }
  exprt start = size(pattern.front());
  for(std::size_t i = 1; i + 1 < pattern.size(); ++i)
  {
    const exprt at =
      payload_index_of(v, payload_literal(pattern[i]), start, dest, loc);
    conditions.push_back(
      binary_relation_exprt{at, ID_ge, from_integer(0, integer_type())});
    parts.bounds.emplace_back(start, at);
    start = plus_exprt{at, size(pattern[i])};
  }
  const exprt end = minus_exprt{length, size(pattern.back())};
  conditions.push_back(binary_relation_exprt{end, ID_ge, start});
  if(!pattern.back().empty())
  {
    conditions.push_back(payload_equal(
      payload_substring(v, end, length, dest, loc),
      payload_literal(pattern.back()),
      dest,
      loc));
  }
  parts.bounds.emplace_back(start, end);
  return conjunction(conditions);
}

exprt shell_convertert::template_parts(
  const template_codet &code,
  const template_partst &parts,
  code_blockt &dest,
  const source_locationt &loc)
{
  // For refined strings, on the bytes of the value between the bounds of
  // each part, rather than on substrings, which the string refinement
  // solver decides more slowly (13 s rather than 4 s with two templates).
  const exprt &v = parts.value;
  const exprt zero = from_integer(0, integer_type());
  exprt::operandst conditions;
  for(std::size_t i = 0; i < parts.bounds.size(); ++i)
  {
    const exprt &start = parts.bounds[i].first;
    const exprt &end = parts.bounds[i].second;
    // No byte of \p set in [start, end).
    const auto none_of = [&](const std::bitset<256> &set) -> exprt
    {
      // Native strings match a regular expression on the part.
      if(native())
      {
        return not_exprt{payload_contains_byte(
          payload_substring(v, start, end, dest, loc), set, dest, loc)};
      }
      const exprt found = payload_index_of_set(v, start, set, dest, loc);
      return or_exprt{
        binary_relation_exprt{found, ID_lt, zero},
        binary_relation_exprt{found, ID_ge, end}};
    };
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
      conditions.push_back(binary_relation_exprt{end, ID_gt, start});
      break;
    case hole_classt::FIELDS:
      forbidden.set();
      for(int c = 0; c < 256; ++c)
      {
        if(
          std::isalnum(c) ||
          std::string{"_./:+@%=- \t"}.find(char(c)) != std::string::npos)
        {
          forbidden.reset(c);
        }
      }
      break;
    case hole_classt::DOUBLE_QUOTED:
      for(const unsigned char c : {'"', '\\', '$', '`'})
        forbidden.set(c);
      break;
    case hole_classt::SINGLE_QUOTED:
      for(const unsigned char c : {'\'', '\\'})
        forbidden.set(c);
      // With native strings, also '\'', which the string refinement solver
      // decides slowly (a probe timed out at 300 s rather than taking 4 s).
      if(native())
      {
        const exprt rest = payload_replace_all(
          payload_substring(v, start, end, dest, loc), "'\\''", "", dest, loc);
        conditions.push_back(
          not_exprt{payload_contains_byte(rest, forbidden, dest, loc)});
        continue;
      }
      break;
    case hole_classt::NAME:
    case hole_classt::NAME_START:
      forbidden.set();
      for(int c = 0; c < 256; ++c)
      {
        if(std::isalnum(c) || c == '_')
          forbidden.reset(c);
      }
      conditions.push_back(binary_relation_exprt{end, ID_gt, start});
      if(code.classes[i] == hole_classt::NAME_START)
      {
        const exprt first = payload_byte(v, start, dest, loc);
        conditions.push_back(not_exprt{and_exprt{
          binary_relation_exprt{first, ID_ge, from_integer('0', first.type())},
          binary_relation_exprt{
            first, ID_le, from_integer('9', first.type())}}});
      }
      break;
    }
    conditions.push_back(none_of(forbidden));
  }
  return conjunction(conditions);
}

void shell_convertert::assign_template_parts(
  const template_codet &code,
  const template_partst &parts,
  code_blockt &dest,
  const source_locationt &loc)
{
  for(std::size_t i = 0; i < parts.bounds.size(); ++i)
  {
    exprt part = payload_substring(
      parts.value, parts.bounds[i].first, parts.bounds[i].second, dest, loc);
    // In single quotes, '\'' is a quoted ' (see template_parts).
    if(code.classes[i] == hole_classt::SINGLE_QUOTED && native())
      part = payload_replace_all(part, "'\\''", "'", dest, loc);
    assign_variable(code.ghosts[i], make_value(part), dest, loc);
  }
}

std::optional<std::pair<std::string, std::string>>
shell_convertert::indirect_name(const std::string &name) const
{
  const auto at = name.find("CPROVERHOLE");
  if(at == std::string::npos)
    return {};
  const auto end = name.find('X', at);
  if(end == std::string::npos)
    return {};
  const std::string placeholder = name.substr(at, end + 1 - at);
  const auto ghost = name_holes.find(placeholder);
  if(ghost == name_holes.end())
    return {};
  return std::make_pair(placeholder, ghost->second);
}

std::vector<std::pair<std::string, std::string>>
shell_convertert::indirect_candidates(const std::string &name) const
{
  std::vector<std::pair<std::string, std::string>> result;
  const auto hole = indirect_name(name);
  const std::size_t at = name.find(hole->first);
  const std::string prefix = name.substr(0, at);
  const std::string suffix = name.substr(at + hole->first.size());
  for(const auto &candidate : variable_names)
  {
    if(
      candidate.size() <= prefix.size() + suffix.size() ||
      candidate.compare(0, prefix.size(), prefix) != 0 ||
      candidate.compare(
        candidate.size() - suffix.size(), suffix.size(), suffix) != 0)
    {
      continue;
    }
    const std::string part = candidate.substr(
      prefix.size(), candidate.size() - prefix.size() - suffix.size());
    result.emplace_back(candidate, part);
  }
  return result;
}

/// \p text with the placeholder \p placeholder as *.
static std::string with_star(std::string text, const std::string &placeholder)
{
  const auto at = text.find(placeholder);
  if(at != std::string::npos)
    text.replace(at, placeholder.size(), "*");
  return text;
}

exprt shell_convertert::expand_indirect_parameter(
  const shell_word_partt &part,
  shell_word_contextt context,
  bool quoted,
  code_blockt &dest)
{
  const source_locationt &loc = part.location;
  const auto hole = indirect_name(part.text);
  shell_word_partt ghost;
  ghost.kind = shell_word_partt::kindt::PARAMETER;
  ghost.text = hole->second;
  ghost.location = loc;
  const exprt name =
    expand_parameter(ghost, shell_word_contextt::CASE_SUBJECT, true, dest);
  const symbol_exprt result = temporary(string_type(), dest, loc);
  // Another name is of a variable that the script does not use otherwise:
  // set from the environment, or not set.
  code_blockt otherwise;
  {
    // A fresh value for each expansion: different names may be expanded
    // here and elsewhere.
    shell_word_partt unknown = part;
    unknown.text = "__cprover_other_variable";
    if(
      part.parameter_operator == "=" || part.parameter_operator == ":=" ||
      part.parameter_operator == "?" || part.parameter_operator == ":?")
    {
      check(
        otherwise,
        false_exprt{},
        "shell-unsupported",
        "the name in " + with_star(display({part}), hole->first) +
          " is of a variable of the script (others are not supported with "
          "this operator)",
        loc);
      add(otherwise, code_assumet{false_exprt{}}, loc);
    }
    else
    {
      assign(
        otherwise,
        variable(unknown.text, loc),
        environment_string(otherwise, loc),
        loc);
      assign(
        otherwise,
        result,
        expand_parameter(unknown, context, quoted, otherwise),
        loc);
    }
  }
  const auto candidates = indirect_candidates(part.text);
  for(auto it = candidates.rbegin(); it != candidates.rend(); ++it)
  {
    shell_word_partt direct = part;
    direct.text = it->first;
    code_blockt branch;
    if(!try_convert(
         [&](code_blockt &block)
         {
           assign(
             block,
             result,
             expand_parameter(direct, context, quoted, block),
             loc);
         },
         branch))
    {
      check(
        branch,
        false_exprt{},
        "shell-unsupported",
        "the name in " + with_star(display({part}), hole->first) + " is not " +
          it->first + " (whose expansion is not supported here)",
        loc);
      add(branch, code_assumet{false_exprt{}}, loc);
    }
    code_ifthenelset choice{
      equal(name, string_literal(it->second, loc), dest, loc),
      std::move(branch),
      std::move(otherwise)};
    otherwise = code_blockt{};
    add(otherwise, std::move(choice), loc);
  }
  dest.append(otherwise);
  return result;
}

bool shell_convertert::convert_indirect_assignment(
  const shell_commandt &command,
  code_blockt &dest)
{
  std::size_t indirect = 0;
  const shell_assignmentt *assignment = nullptr;
  for(const auto &a : command.assignments)
  {
    if(indirect_name(a.name).has_value())
    {
      ++indirect;
      assignment = &a;
    }
  }
  if(indirect == 0)
    return false;
  const source_locationt &loc = command.location;
  if(indirect > 1 || command.assignments.size() > 1)
    unsupported(loc, "more than one assignment with an indirect name");
  const auto hole = indirect_name(assignment->name);
  shell_word_partt ghost;
  ghost.kind = shell_word_partt::kindt::PARAMETER;
  ghost.text = hole->second;
  ghost.location = loc;
  const exprt name =
    expand_parameter(ghost, shell_word_contextt::CASE_SUBJECT, true, dest);
  // Assigning a variable that the script does not use otherwise would not
  // be seen by a later expansion of it by an indirect name, nor by text
  // that is parsed later.
  code_blockt otherwise;
  check(
    otherwise,
    false_exprt{},
    "shell-unsupported",
    "the name of the assignment to " +
      with_star(assignment->name, hole->first) +
      " is of a variable of the script (others are not supported)",
    loc);
  add(otherwise, code_assumet{false_exprt{}}, loc);
  const auto before = known_literals;
  const auto candidates = indirect_candidates(assignment->name);
  for(auto it = candidates.rbegin(); it != candidates.rend(); ++it)
  {
    shell_commandt direct = command;
    direct.assignments.front().name = it->first;
    code_blockt branch;
    known_literals = before;
    if(!try_convert(
         [&](code_blockt &block) { convert_assignments_only(direct, block); },
         branch))
    {
      check(
        branch,
        false_exprt{},
        "shell-unsupported",
        "the name of the assignment is not " + it->first +
          " (whose assignment is not supported here)",
        loc);
      add(branch, code_assumet{false_exprt{}}, loc);
    }
    code_ifthenelset choice{
      equal(name, string_literal(it->second, loc), dest, loc),
      std::move(branch),
      std::move(otherwise)};
    otherwise = code_blockt{};
    add(otherwise, std::move(choice), loc);
  }
  dest.append(otherwise);
  known_literals.clear();
  return true;
}

/// Whether \p text is a name (XCU 3.216).
static bool is_variable_name(const std::string &text)
{
  if(text.empty() || std::isdigit(static_cast<unsigned char>(text[0])))
    return false;
  for(const char c : text)
  {
    if(!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
      return false;
  }
  return true;
}

void shell_convertert::collect_variable_names(
  const std::vector<shell_word_partt> &parts)
{
  for(const auto &part : parts)
  {
    if(
      part.kind == shell_word_partt::kindt::PARAMETER &&
      is_variable_name(part.text))
      variable_names.insert(part.text);
    collect_variable_names(part.parts);
    collect_variable_names(part.index);
    collect_variable_names(part.replacement);
    if(part.commands != nullptr)
      collect_variable_names(*part.commands);
  }
}

void shell_convertert::collect_variable_names(const shell_command_listt &list)
{
  static const std::set<std::string> declaring{
    "read",
    "getopts",
    "export",
    "local",
    "readonly",
    "declare",
    "typeset",
    "unset"};
  for(const auto &command : list)
  {
    if(
      is_variable_name(command.name) &&
      command.kind == shell_commandt::kindt::FOR)
      variable_names.insert(command.name);
    for(const auto &assignment : command.assignments)
    {
      if(is_variable_name(assignment.name))
        variable_names.insert(assignment.name);
      collect_variable_names(assignment.value);
      for(const auto &element : assignment.elements)
        collect_variable_names(element);
    }
    const auto name =
      command.words.empty() ? std::nullopt : static_text(command.words.front());
    for(const auto &word : command.words)
    {
      collect_variable_names(word);
      if(name.has_value() && declaring.count(*name) != 0)
      {
        if(const auto text = static_text(word))
        {
          const std::string before = text->substr(0, text->find('='));
          if(is_variable_name(before))
            variable_names.insert(before);
        }
      }
    }
    for(const auto &redirect : command.redirects)
    {
      collect_variable_names(redirect.target);
      collect_variable_names(redirect.here_document);
    }
    collect_variable_names(command.body);
    collect_variable_names(command.condition);
    collect_variable_names(command.else_body);
    for(const auto &item : command.for_items)
      collect_variable_names(item);
    collect_variable_names(command.case_word);
    for(const auto &item : command.case_items)
    {
      for(const auto &pattern : item.patterns)
        collect_variable_names(pattern);
      collect_variable_names(item.body);
    }
  }
}
