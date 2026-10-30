/// \file
/// Shell converter: pattern matching (POSIX.1-2024 XCU 2.14), for case and
/// for the pattern removal expansions ${x#p}, ${x##p}, ${x%p}, ${x%%p}.
///
/// Each distinct pattern gets C matcher functions (shell_pattern.h), which
/// are generated once and added to the symbol table. Patterns that only
/// contain quoted expansions and ordinary characters are compared as
/// strings instead. Patterns with both pattern characters and expansions
/// are not supported yet.

#include "shell_convert_class.h"

#include "shell_pattern.h"

shell_patternt shell_convertert::compile_pattern(
  const shell_wordt &word,
  const source_locationt &loc)
{
  std::string error;
  auto pattern = compile_shell_pattern(word, program.dialect, error);
  if(!pattern.has_value())
    unsupported(loc, error);
  if(!pattern->is_literal() && !pattern->dynamic_parts.empty())
  {
    unsupported(
      loc, "pattern with both pattern characters and quoted expansions");
  }
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

const std::string &
shell_convertert::pattern_prefix(const shell_patternt &pattern)
{
  const std::string signature = pattern.signature();
  const auto found = pattern_matchers.find(signature);
  if(found != pattern_matchers.end())
    return found->second;
  const std::string prefix = CPROVER_PREFIX "shell_pattern_" +
                             std::to_string(pattern_matchers.size() + 1);
  const auto names = shell_pattern_function_names(prefix);
  if(add_shell_runtime_code(
       shell_pattern_matcher_source(pattern, prefix, max_length),
       {names.match,
        names.remove_shortest_prefix,
        names.remove_longest_prefix,
        names.remove_shortest_suffix,
        names.remove_longest_suffix},
       symbol_table,
       log.get_message_handler()))
  {
    throw shell_conversion_errort{};
  }
  return pattern_matchers.emplace(signature, prefix).first->second;
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
  if(!bounded())
    return unbounded_pattern_matches(pattern, subject, dest, loc);
  const auto names = shell_pattern_function_names(pattern_prefix(pattern));
  const symbol_exprt result = call(names.match, {subject}, dest, loc);
  return notequal_exprt{result, from_integer(0, result.type())};
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
    if(!bounded())
    {
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
    const symbol_exprt found = call(
      prefix ? SHELL_RT_HAS_PREFIX : SHELL_RT_HAS_SUFFIX,
      {value, literal},
      dest,
      loc);
    const exprt rest = minus_exprt{length_of(value), length_of(literal)};
    const symbol_exprt removed = call(
      SHELL_RT_SUBSTRING,
      {value,
       prefix ? exprt{length_of(literal)}
              : exprt{from_integer(0, length_of(value).type())},
       rest},
      dest,
      loc);
    return if_exprt{
      notequal_exprt{found, from_integer(0, found.type())}, removed, value};
  }
  if(!bounded())
    return unbounded_remove_pattern(pattern, value, prefix, longest, dest, loc);
  const auto names = shell_pattern_function_names(pattern_prefix(pattern));
  const std::string &function =
    prefix
      ? (longest ? names.remove_longest_prefix : names.remove_shortest_prefix)
      : (longest ? names.remove_longest_suffix : names.remove_shortest_suffix);
  return call(function, {value}, dest, loc);
}
