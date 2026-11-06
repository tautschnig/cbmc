/// \file
/// Shell converter: pattern matching (POSIX.1-2024 XCU 2.14), for case and
/// for the pattern removal expansions ${x#p}, ${x##p}, ${x%p}, ${x%%p}.
///
/// Patterns that only contain quoted expansions and ordinary characters are
/// compared as strings. Other patterns are decomposed into segments between
/// stars (shell_convert_strings.cpp).

#include "shell_convert_class.h"

shell_patternt shell_convertert::compile_pattern(
  const shell_wordt &word,
  const source_locationt &loc)
{
  std::string error;
  auto pattern = compile_shell_pattern(word, program.dialect, error);
  if(!pattern.has_value())
    unsupported(loc, error);
  return *pattern;
}

shell_wordt shell_convertert::prepare_pattern(
  const shell_wordt &word,
  code_blockt &dest,
  const source_locationt &loc)
{
  using kindt = shell_word_partt::kindt;
  shell_wordt result;
  for(const auto &part : word)
  {
    if(
      part.kind != kindt::PARAMETER || part.text == "@" || part.text == "*" ||
      has_side_effects(part))
    {
      result.push_back(part);
      continue;
    }
    // The value of an unquoted expansion is itself a pattern, where shells
    // differ in backslashes, and zsh matches it literally: a value without
    // pattern characters and backslashes matches literally in all of them.
    if(const auto literal = known_literal(part))
    {
      if(literal->find('\\') == std::string::npos)
      {
        shell_word_partt text = part;
        text.kind = program.dialect == shell_dialectt::ZSH
                      ? kindt::QUOTED_LITERAL
                      : kindt::LITERAL;
        text.text = *literal;
        text.parts.clear();
        result.push_back(text);
        continue;
      }
      result.push_back(part);
      continue;
    }
    check_literal_in_pattern(
      expand_part(part, shell_word_contextt::CASE_SUBJECT, false, false, dest),
      part,
      dest);
    shell_word_partt quoted;
    quoted.kind = kindt::DOUBLE_QUOTED;
    quoted.location = part.location;
    quoted.parts.push_back(part);
    result.push_back(quoted);
  }
  (void)loc;
  return result;
}

void shell_convertert::check_literal_in_pattern(
  const exprt &value,
  const shell_word_partt &part,
  code_blockt &dest)
{
  if(program.dialect == shell_dialectt::ZSH)
    return;
  std::bitset<256> special;
  for(unsigned char c : {'*', '?', '[', '\\'})
    special.set(c);
  check(
    dest,
    not_exprt{
      payload_contains_byte(payload_of(value), special, dest, part.location)},
    "shell-unsupported",
    "the value of the unquoted " + display({part}) +
      " has no *, ?, [ or \\ (as a pattern, shells differ in its "
      "backslashes; it is not supported)",
    part.location);
}

/// The string a literal pattern matches: its characters, and the values of
/// its quoted expansions.
exprt shell_convertert::literal_pattern_string(
  const shell_patternt &pattern,
  code_blockt &dest,
  const source_locationt &loc)
{
  using kindt = shell_pattern_elementt::kindt;
  std::optional<exprt> result;
  std::string pending;
  auto append = [&](exprt value)
  { result = result.has_value() ? concat(*result, value, dest, loc) : value; };
  for(const auto &element : pattern.elements)
  {
    if(element.kind == kindt::CHARACTER)
    {
      pending += static_cast<char>(element.character);
      continue;
    }
    if(!pending.empty())
    {
      append(string_literal(pending, loc));
      pending.clear();
    }
    append(expand_part(
      *pattern.dynamic_parts[element.dynamic_index],
      shell_word_contextt::CASE_SUBJECT,
      true,
      false,
      dest));
  }
  if(!pending.empty() || !result.has_value())
    append(string_literal(pending, loc));
  return *result;
}

exprt shell_convertert::template_matches(
  const dynamic_templatet &pattern,
  const exprt &subject,
  code_blockt &dest,
  const source_locationt &loc)
{
  if(pattern.size() == 1)
    return equal(subject, string_literal(pattern.front(), loc), dest, loc);
  shell_patternt glob;
  for(std::size_t i = 0; i < pattern.size(); ++i)
  {
    if(i != 0)
    {
      shell_pattern_elementt star;
      star.kind = shell_pattern_elementt::kindt::STAR;
      glob.elements.push_back(star);
    }
    for(const char c : pattern[i])
    {
      shell_pattern_elementt character;
      character.character = static_cast<unsigned char>(c);
      glob.elements.push_back(character);
    }
  }
  if(pattern.size() == 2 && pattern[0].empty() && pattern[1].empty())
    return true_exprt{};
  if(native())
    return unbounded_pattern_matches(glob, subject, dest, loc);
  template_partst parts;
  return template_decomposition(pattern, subject, parts, dest, loc);
}

exprt shell_convertert::pattern_matches(
  const shell_wordt &word,
  const exprt &subject,
  code_blockt &dest,
  const source_locationt &loc)
{
  // The pattern refers to the parts of the prepared word.
  const shell_wordt prepared = prepare_pattern(word, dest, loc);
  const shell_patternt pattern = compile_pattern(prepared, loc);
  if(pattern.is_literal())
    return equal(
      subject, literal_pattern_string(pattern, dest, loc), dest, loc);
  if(
    pattern.elements.size() == 1 &&
    pattern.elements[0].kind == shell_pattern_elementt::kindt::STAR)
  {
    return true_exprt{};
  }
  return unbounded_pattern_matches(pattern, subject, dest, loc);
}

exprt shell_convertert::remove_pattern(
  const exprt &value,
  const std::string &op,
  const shell_wordt &word,
  code_blockt &dest,
  const source_locationt &loc)
{
  const bool prefix = op[0] == '#';
  const bool longest = op.size() == 2;
  // The pattern refers to the parts of the prepared word.
  const shell_wordt prepared = prepare_pattern(word, dest, loc);
  const shell_patternt pattern = compile_pattern(prepared, loc);
  if(pattern.is_literal())
  {
    // A literal pattern matches one string: shortest and longest agree.
    const exprt literal = literal_pattern_string(pattern, dest, loc);
    const exprt s = payload_of(value);
    const exprt l = payload_of(literal);
    const exprt found = prefix ? payload_has_prefix(s, l, dest, loc)
                               : payload_has_suffix(s, l, dest, loc);
    const exprt length = payload_length(s, dest, loc);
    const exprt rest = minus_exprt{length, payload_length(l, dest, loc)};
    const exprt zero = from_integer(0, integer_type());
    const exprt start = prefix ? exprt{minus_exprt{length, rest}} : zero;
    const exprt end = prefix ? length : rest;
    return make_value(payload_substring(
      s,
      if_exprt{found, start, zero},
      if_exprt{found, end, length},
      dest,
      loc));
  }
  return unbounded_remove_pattern(pattern, value, prefix, longest, dest, loc);
}
