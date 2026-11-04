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

exprt shell_convertert::pattern_matches(
  const shell_wordt &word,
  const exprt &subject,
  code_blockt &dest,
  const source_locationt &loc)
{
  const shell_patternt pattern = compile_pattern(word, loc);
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
  const shell_patternt pattern = compile_pattern(word, loc);
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
