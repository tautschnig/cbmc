/// \file
/// Shell converter: specifications of the output of utilities, for command
/// substitutions whose commands are one call of such a utility.
///
/// Without a specification, the output of a command substitution is any
/// string. A specification constrains it, which is an assumption about the
/// utility that is found: the one of that name that the platform provides.
/// Each specification covers the implementations of the platform (GNU
/// coreutils, BSD and macOS, busybox), as the union of their outputs where
/// they differ, and does not apply where the script defines a function of
/// that name, or where the arguments are not supported.
///
/// mktemp [-d] [-q] [-u] [-t] [-p DIR | --tmpdir[=DIR]] [TEMPLATE]: the
/// name of the file or directory that it created, or nothing if it fails.
/// The trailing X's of the template (at least three) become letters and
/// digits. Without a template, the template is tmp. and 10 (GNU), 8 (BSD)
/// or 6 (busybox) X's. Without a template, with -t, or with -p, the name is
/// in a directory: $TMPDIR if it is set and not empty, the argument of -p,
/// or /tmp; the implementations differ in the order of these. BSD's -t
/// PREFIX also makes PREFIX.XXXXXXXX in that directory.

#include "shell_convert_class.h"

#include <util/std_code.h>

#include <bitset>
#include <cctype>

namespace
{
/// A name that mktemp may make: a directory (or none), and a file name of
/// a static prefix and a number of letters and digits.
struct mktemp_namet
{
  /// Whether the name is in one of the temporary directories.
  bool in_directory = false;
  std::string prefix;
  std::size_t random = 0;
};
} // namespace

/// The parts of a template that ends in at least three X's: the text before
/// them and their number.
static std::optional<std::pair<std::string, std::size_t>>
split_template(const std::string &text)
{
  std::size_t x = 0;
  while(x < text.size() && text[text.size() - 1 - x] == 'X')
    ++x;
  if(x < 3)
    return {};
  return std::make_pair(text.substr(0, text.size() - x), x);
}

std::optional<exprt> shell_convertert::utility_output(
  const shell_word_partt &part,
  code_blockt &dest)
{
  if(part.commands == nullptr || part.commands->size() != 1)
    return {};
  const shell_commandt &command = part.commands->front();
  if(
    command.kind != shell_commandt::kindt::SIMPLE || command.negated ||
    command.background || !command.assignments.empty() || command.words.empty())
  {
    return {};
  }
  // Only standard error may be redirected.
  for(const auto &redirect : command.redirects)
  {
    if(redirect.fd != 2)
      return {};
  }
  const auto name = static_text(command.words.front());
  if(!name.has_value() || function_definitions.count(*name) != 0)
    return {};
  if(*name == "mktemp")
    return mktemp_output(command, dest);
  return {};
}

std::optional<exprt> shell_convertert::mktemp_output(
  const shell_commandt &command,
  code_blockt &dest)
{
  const source_locationt &loc = command.location;
  // The options, which must be literal text.
  bool tmpdir = false;
  bool t_option = false;
  std::optional<std::string> dir_argument;
  std::optional<std::string> template_text;
  for(std::size_t i = 1; i < command.words.size(); ++i)
  {
    const auto word = static_text(command.words[i]);
    if(!word.has_value())
      return {};
    if(template_text.has_value())
      return {};
    if(
      *word == "-d" || *word == "--directory" || *word == "-q" ||
      *word == "--quiet" || *word == "-u" || *word == "--dry-run")
    {
      continue;
    }
    if(*word == "-t")
    {
      t_option = true;
      continue;
    }
    if(*word == "-p" && i + 1 < command.words.size())
    {
      const auto dir = static_text(command.words[++i]);
      if(!dir.has_value())
        return {};
      tmpdir = true;
      dir_argument = *dir;
      continue;
    }
    if(*word == "--tmpdir")
    {
      tmpdir = true;
      continue;
    }
    if(word->rfind("--tmpdir=", 0) == 0)
    {
      tmpdir = true;
      dir_argument = word->substr(9);
      continue;
    }
    if(word->empty() || (*word)[0] == '-')
      return {};
    template_text = *word;
  }

  // The names, and whether each is in a directory.
  std::vector<mktemp_namet> names;
  if(!template_text.has_value())
  {
    for(const std::size_t random : {10, 8, 6})
      names.push_back({true, "tmp.", random});
  }
  else
  {
    const auto parts = split_template(*template_text);
    const bool in_directory = tmpdir || t_option;
    if(parts.has_value())
    {
      // A template in a directory must be a file name there.
      if(!in_directory || template_text->find('/') == std::string::npos)
        names.push_back({in_directory, parts->first, parts->second});
    }
    if(t_option && template_text->find('/') == std::string::npos)
      names.push_back({true, *template_text + ".", 8});
    if(names.empty())
      return {};
  }

  // The directories: $TMPDIR if set and not empty, the argument of -p, and
  // /tmp.
  shell_word_partt tmpdir_part;
  tmpdir_part.kind = shell_word_partt::kindt::PARAMETER;
  tmpdir_part.text = "TMPDIR";
  tmpdir_part.location = loc;
  const exprt tmpdir_value = expand_parameter(
    tmpdir_part, shell_word_contextt::CASE_SUBJECT, true, dest);
  std::vector<std::pair<exprt, exprt>> directories{
    {not_exprt{is_empty(tmpdir_value)}, payload_of(tmpdir_value)},
    {true_exprt{}, payload_literal("/tmp")}};
  if(dir_argument.has_value() && !dir_argument->empty())
    directories.emplace_back(true_exprt{}, payload_literal(*dir_argument));

  // One name, as one concatenation of parts that are chosen among the
  // possible ones: a directory and a slash (where the directory ends with
  // one, implementations differ in whether they add another), the prefix
  // of the file name, and the letters and digits.
  const auto choose = [&](
                        const symbol_exprt &target,
                        const std::vector<std::pair<exprt, exprt>> &options,
                        code_blockt &block)
  {
    code_blockt chosen;
    for(auto it = options.rbegin(); it != options.rend(); ++it)
    {
      code_blockt option;
      add(option, code_assumet{it->first}, loc);
      assign(option, target, it->second, loc);
      if(it == options.rbegin())
        chosen = std::move(option);
      else
      {
        code_blockt either;
        add(
          either,
          code_ifthenelset{
            nondet(bool_typet{}, loc), std::move(option), std::move(chosen)},
          loc);
        chosen = std::move(either);
      }
    }
    block.append(chosen);
  };
  code_blockt choices;
  bool any_directory = false;
  bool any_bare = false;
  for(const auto &name : names)
  {
    any_directory |= name.in_directory;
    any_bare |= !name.in_directory;
  }
  // Names in a directory and names without one come from different
  // templates; only the cases that occur are modelled, which keeps the
  // values precise for the analysis of dynamic sites.
  const symbol_exprt in_directory = temporary(bool_typet{}, choices, loc);
  if(any_directory && any_bare)
    assign(choices, in_directory, nondet(bool_typet{}, loc), loc);
  else
    assign(
      choices,
      in_directory,
      constant_exprt{any_directory ? ID_true : ID_false, bool_typet{}},
      loc);
  std::optional<symbol_exprt> directory;
  if(any_directory)
  {
    directory = temporary(payload_type(), choices, loc);
    code_blockt with;
    choose(*directory, directories, with);
    const symbol_exprt separator = temporary(payload_type(), with, loc);
    choose(
      separator,
      {{true_exprt{}, payload_literal("/")},
       {payload_has_suffix(*directory, payload_literal("/"), with, loc),
        payload_literal("")}},
      with);
    assign(
      with, *directory, payload_concat(*directory, separator, with, loc), loc);
    code_blockt without;
    assign(without, *directory, payload_literal(""), loc);
    if(any_bare)
    {
      add(
        choices,
        code_ifthenelset{in_directory, std::move(with), std::move(without)},
        loc);
    }
    else
      choices.append(with);
  }
  const symbol_exprt prefix = temporary(payload_type(), choices, loc);
  const symbol_exprt random_length = temporary(integer_type(), choices, loc);
  {
    std::vector<std::pair<exprt, exprt>> prefixes;
    std::vector<std::pair<exprt, exprt>> lengths;
    code_blockt chosen;
    for(auto it = names.rbegin(); it != names.rend(); ++it)
    {
      code_blockt option;
      add(
        option,
        code_assumet{
          it->in_directory ? exprt{in_directory}
                           : exprt{not_exprt{in_directory}}},
        loc);
      assign(option, prefix, payload_literal(it->prefix), loc);
      assign(
        option, random_length, from_integer(it->random, integer_type()), loc);
      if(it == names.rbegin())
        chosen = std::move(option);
      else
      {
        code_blockt either;
        add(
          either,
          code_ifthenelset{
            nondet(bool_typet{}, loc), std::move(option), std::move(chosen)},
          loc);
        chosen = std::move(either);
      }
    }
    choices.append(chosen);
  }
  // Letters and digits.
  std::bitset<256> other;
  other.set();
  for(int c = 0; c < 256; ++c)
  {
    if(std::isalnum(c))
      other.reset(c);
  }
  const exprt random = payload_of(nondet_string(choices, loc));
  add(
    choices,
    code_assumet{and_exprt{
      equal_exprt{payload_length(random, choices, loc), random_length},
      not_exprt{payload_contains_byte(random, other, choices, loc)}}},
    loc);
  const symbol_exprt result = temporary(string_type(), dest, loc);
  const exprt name = payload_concat(prefix, random, choices, loc);
  assign(
    choices,
    result,
    make_value(
      directory.has_value() ? payload_concat(*directory, name, choices, loc)
                            : name),
    loc);
  // Nothing on standard output where it fails.
  code_blockt failed;
  assign(failed, result, string_literal("", loc), loc);
  add(
    dest,
    code_ifthenelset{
      equal_exprt{substitution_status(), from_integer(0, status_type())},
      std::move(choices),
      std::move(failed)},
    loc);
  return exprt{result};
}
