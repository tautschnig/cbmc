/// \file
/// Shell converter: IFS (XCU 2.5.3) and field splitting (XCU 2.6.5).
///
/// Every shell sets IFS to space, tab and newline at startup. IFS has no
/// symbol: its value is tracked during conversion, as a known literal
/// (see known_literals), and assignments of other than literals are not
/// supported. Where its value is not known, as at the start of a
/// function, the ghost symbol shell::ifs_default records whether IFS has
/// its default value, which field splitting there checks.

#include "shell_convert_class.h"

/// The key of known_literals that records that IFS is unset.
static const char ifs_unset_key[] = "IFS unset";

static const char default_ifs[] = " \t\n";

std::string shell_convertert::initial_ifs() const
{
  // zsh adds a null byte, which is not IFS white space.
  if(program.dialect == shell_dialectt::ZSH)
    return std::string{" \t\n\0", 4};
  return default_ifs;
}

symbol_exprt shell_convertert::ifs_default()
{
  return global_symbol("shell::ifs_default", "ifs_default", bool_typet{});
}

symbol_exprt shell_convertert::ifs_unset()
{
  return global_symbol("shell::ifs_unset", "ifs_unset", bool_typet{});
}

std::optional<std::string> shell_convertert::known_ifs() const
{
  if(known_literals.count(ifs_unset_key) != 0)
    return initial_ifs();
  const auto found = known_literals.find("IFS");
  if(found == known_literals.end())
    return {};
  return found->second;
}

std::string shell_convertert::ifs_for_splitting(
  code_blockt &dest,
  const source_locationt &loc)
{
  if(const auto ifs = known_ifs())
    return *ifs;
  check(
    dest,
    ifs_default(),
    "shell-unsupported",
    "IFS has its default value here (field splitting with an IFS that is "
    "not known here is not supported)",
    loc);
  return initial_ifs();
}

void shell_convertert::assign_ifs(
  const exprt &value,
  code_blockt &dest,
  const source_locationt &loc)
{
  const auto literal = literal_of(value);
  if(!literal.has_value())
    unsupported(loc, "assignment of a value that is not known here to IFS");
  known_literals["IFS"] = *literal;
  known_literals.erase(ifs_unset_key);
  assign(
    dest,
    ifs_default(),
    *literal == initial_ifs() ? exprt{true_exprt{}} : exprt{false_exprt{}},
    loc);
  assign(dest, ifs_unset(), false_exprt{}, loc);
}

void shell_convertert::unset_ifs(code_blockt &dest, const source_locationt &loc)
{
  // Unset, IFS splits as by default.
  known_literals.erase("IFS");
  known_literals[ifs_unset_key] = "";
  assign(dest, ifs_default(), true_exprt{}, loc);
  assign(dest, ifs_unset(), true_exprt{}, loc);
}

std::optional<std::string>
shell_convertert::ifs_value(code_blockt &dest, const source_locationt &loc)
{
  if(known_literals.count(ifs_unset_key) != 0)
    return {};
  const auto found = known_literals.find("IFS");
  if(found != known_literals.end())
    return found->second;
  // Not known here, as in a function: expansions are supported where IFS
  // is set to its default value, as it is at startup.
  check(
    dest,
    and_exprt{ifs_default(), not_exprt{ifs_unset()}},
    "shell-unsupported",
    "IFS is set to its default value here (the expansion of an IFS that is "
    "not known here is not supported)",
    loc);
  return initial_ifs();
}

std::vector<std::string> shell_convertert::split_known_parameter(
  const std::string &value,
  const std::string &ifs) const
{
  // zsh does not split parameter expansions (unless SH_WORD_SPLIT is set,
  // which setopt and emulate, which are not supported, would do).
  if(program.dialect == shell_dialectt::ZSH)
    return value.empty() ? std::vector<std::string>{}
                         : std::vector<std::string>{value};
  return split_literal(value, ifs);
}

std::vector<std::string> shell_convertert::split_literal(
  const std::string &value,
  const std::string &ifs)
{
  if(ifs.empty())
    return value.empty() ? std::vector<std::string>{}
                         : std::vector<std::string>{value};
  const auto is_ifs = [&](char c) { return ifs.find(c) != std::string::npos; };
  const auto is_white = [&](char c)
  { return is_ifs(c) && (c == ' ' || c == '\t' || c == '\n'); };
  std::size_t i = 0, n = value.size();
  // IFS white space at the start and the end is ignored.
  while(i < n && is_white(value[i]))
    ++i;
  while(n > i && is_white(value[n - 1]))
    --n;
  std::vector<std::string> fields;
  std::string current;
  while(i < n)
  {
    const char c = value[i];
    if(!is_ifs(c))
    {
      current += c;
      ++i;
      continue;
    }
    // A delimiter: IFS white space, or one other IFS byte with the IFS
    // white space around it.
    while(i < n && is_white(value[i]))
      ++i;
    if(i < n && is_ifs(value[i]) && !is_white(value[i]))
    {
      ++i;
      while(i < n && is_white(value[i]))
        ++i;
    }
    fields.push_back(current);
    current.clear();
  }
  if(!current.empty())
    fields.push_back(current);
  return fields;
}

bool shell_convertert::ifs_is_white(const std::string &ifs)
{
  return ifs.find_first_not_of(default_ifs) == std::string::npos;
}

bool shell_convertert::ifs_has_white(const std::string &ifs)
{
  return ifs.find_first_of(default_ifs) != std::string::npos;
}

std::bitset<256> shell_convertert::ifs_set(const std::string &ifs)
{
  std::bitset<256> set;
  for(char c : ifs)
    set.set(static_cast<unsigned char>(c));
  return set;
}
