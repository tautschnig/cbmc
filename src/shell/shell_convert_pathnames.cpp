/// \file
/// Shell converter: pathname expansion (XCU 2.6.6) in lists of fields.
/// The file system is not modelled: a word with a pattern yields either
/// itself, after quote removal, where no pathname matches, or some
/// pathnames, each of which matches the word as a case pattern. Pathname
/// matching (XCU 2.14.3) also requires slashes and leading periods to be
/// matched explicitly, which only removes pathnames.

#include "shell_convert_class.h"

#include "shell_pattern.h"

std::optional<shell_wordt>
shell_convertert::pathname_pattern(const shell_wordt &word) const
{
  using kindt = shell_word_partt::kindt;
  // Unquoted parameter expansions, which yield one field without pattern
  // characters (see expand_pathnames), are matched literally, as if they
  // were quoted.
  // Expansions are evaluated again for each pathname.
  if(has_side_effects(word))
    return {};
  shell_wordt quoted;
  bool unquoted_pattern = false;
  for(const auto &part : word)
  {
    if(part.kind == kindt::PARAMETER)
    {
      if(part.text == "@" || part.text == "*")
        return {};
      shell_word_partt wrapped;
      wrapped.kind = kindt::DOUBLE_QUOTED;
      wrapped.location = part.location;
      wrapped.parts.push_back(part);
      quoted.push_back(wrapped);
      continue;
    }
    if(part.kind == kindt::COMMAND_SUBSTITUTION)
      return {};
    if(part.kind == kindt::LITERAL)
    {
      for(std::size_t i = 0; i < part.text.size(); ++i)
      {
        if(part.text[i] == '\\')
          ++i;
        else if(
          part.text[i] == '*' || part.text[i] == '?' || part.text[i] == '[')
        {
          unquoted_pattern = true;
        }
      }
    }
    quoted.push_back(part);
  }
  if(!unquoted_pattern)
    return {};
  std::string error;
  const auto pattern = compile_shell_pattern(quoted, program.dialect, error);
  if(!pattern.has_value() || pattern->is_literal())
    return {};
  return quoted;
}

void shell_convertert::expand_pathnames(
  const field_listt &list,
  const shell_wordt &word,
  const shell_wordt &pattern,
  code_blockt &dest,
  const source_locationt &loc)
{
  using kindt = shell_word_partt::kindt;
  // zsh reports patterns that match nothing (NOMATCH).
  if(program.dialect == shell_dialectt::ZSH)
    unsupported(loc, "pathname expansion in zsh");
  // The unquoted expansions yield one field without pattern characters.
  for(const auto &part : word)
  {
    if(part.kind == kindt::PARAMETER)
    {
      check_literal_in_pattern(
        expand_part(part, shell_word_contextt::FIELD, false, false, dest),
        part,
        dest);
    }
  }
  const exprt text =
    expand_word(pattern, shell_word_contextt::CASE_SUBJECT, dest, loc);

  // No pathname matches, or set -f: the word itself.
  code_blockt unmatched;
  append_field(list, text, unmatched, loc);

  // Some pathnames match: they are not empty, and match the pattern. One
  // more than the bound is reported by check_field_list.
  code_blockt matched;
  const symbol_exprt count = temporary(count_type(), matched, loc);
  assign(matched, count, nondet(count_type(), loc), loc);
  add(
    matched,
    code_assumet{and_exprt{
      binary_relation_exprt{count, ID_ge, from_integer(1, count_type())},
      binary_relation_exprt{
        count, ID_le, from_integer(max_arguments + 1, count_type())}}},
    loc);
  for(std::size_t i = 0; i <= max_arguments; ++i)
  {
    code_blockt append;
    const exprt name = nondet_string(append, loc);
    add(
      append,
      code_assumet{and_exprt{
        not_exprt{is_empty(name)},
        pattern_matches(pattern, name, append, loc)}},
      loc);
    append_field(list, name, append, loc);
    add(
      matched,
      code_ifthenelset{
        binary_relation_exprt{from_integer(i, count_type()), ID_lt, count},
        std::move(append)},
      loc);
  }

  exprt expand = nondet(bool_typet{}, loc);
  if(option_possible("noglob"))
    expand = and_exprt{not_exprt{option("noglob")}, expand};
  add(
    dest,
    code_ifthenelset{expand, std::move(matched), std::move(unmatched)},
    loc);
}
