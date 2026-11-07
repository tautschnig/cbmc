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
///
/// getopt [-a] [-q] [-n NAME] [-s sh|bash] -o OPTSTRING [-l LONGOPTS]
/// [--] PARAMETERS (with native strings): where it succeeds or reports
/// invalid options (status 0 or 1), and GETOPT_COMPATIBLE is not set, the
/// options that it recognised, each with its argument if it has one, then
/// --, then the other parameters, each preceded by a space, and each
/// argument and parameter in single quotes with '\'' for each '
/// (util-linux and busybox; BSD and macOS getopt do not support -o, and
/// scripts that use it require one of these), or nothing.

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
  if(*name == "getopt")
    return getopt_output(command, dest);
  return {};
}

/// The option of getopt that \p word names, as --NAME, -N or --N... (an
/// unambiguous abbreviation of NAME), with its attached argument, if any.
static std::optional<std::pair<char, std::optional<std::string>>>
getopt_option(const std::string &word)
{
  static const std::vector<std::pair<std::string, char>> long_names{
    {"alternative", 'a'},
    {"help", 'h'},
    {"longoptions", 'l'},
    {"name", 'n'},
    {"options", 'o'},
    {"quiet", 'q'},
    {"quiet-output", 'Q'},
    {"shell", 's'},
    {"test", 'T'},
    {"unknown", 'U'},
    {"unquoted", 'u'},
    {"version", 'V'}};
  if(word.size() >= 3 && word[0] == '-' && word[1] == '-')
  {
    const auto equals = word.find('=');
    const std::string name = word.substr(2, equals - 2);
    std::optional<std::string> argument;
    if(equals != std::string::npos)
      argument = word.substr(equals + 1);
    std::optional<char> found;
    for(const auto &option : long_names)
    {
      if(option.first == name)
        return std::make_pair(option.second, argument);
      if(option.first.compare(0, name.size(), name) == 0)
      {
        if(found.has_value())
          return {};
        found = option.second;
      }
    }
    if(!found.has_value())
      return {};
    return std::make_pair(*found, argument);
  }
  if(word.size() >= 2 && word[0] == '-' && word[1] != '-')
  {
    std::optional<std::string> argument;
    if(word.size() > 2)
      argument = word.substr(2);
    return std::make_pair(word[1], argument);
  }
  return {};
}

std::optional<exprt> shell_convertert::getopt_output(
  const shell_commandt &command,
  code_blockt &dest)
{
  const source_locationt &loc = command.location;
  std::optional<std::string> optstring;
  std::string longopts;
  // getopt's own options, which must be literal text, except for the
  // name, which is only in messages.
  for(std::size_t i = 1; i < command.words.size(); ++i)
  {
    const auto word = static_text(command.words[i]);
    if(!word.has_value())
      return {};
    if(*word == "--" || word->empty() || (*word)[0] != '-')
      break;
    const auto option = getopt_option(*word);
    if(!option.has_value())
      return {};
    const char letter = option->first;
    if(letter == 'a' || letter == 'q')
    {
      if(option->second.has_value())
        return {};
      continue;
    }
    if(letter != 'o' && letter != 'l' && letter != 'n' && letter != 's')
      return {};
    std::optional<std::string> argument = option->second;
    if(!argument.has_value())
    {
      if(++i == command.words.size())
        return {};
      if(letter == 'n')
        continue;
      argument = static_text(command.words[i]);
      if(!argument.has_value())
        return {};
    }
    if(letter == 'o')
      optstring = *argument;
    else if(letter == 'l')
      longopts += (longopts.empty() ? "" : ",") + *argument;
    else if(letter == 's' && *argument != "sh" && *argument != "bash")
      return {};
  }
  // Without -o, the first parameter is the option string, which this
  // does not support.
  if(!optstring.has_value())
    return {};
  // A leading - outputs the other parameters in place, which this does
  // not support; + and : do not change the output.
  std::string letters = *optstring;
  while(!letters.empty() && (letters[0] == '+' || letters[0] == ':'))
    letters.erase(0, 1);
  if(!letters.empty() && letters[0] == '-')
    return {};

  // The words of the options: each option, and whether it has an
  // argument.
  std::vector<std::pair<std::string, bool>> options;
  for(std::size_t i = 0; i < letters.size(); ++i)
  {
    const char c = letters[i];
    // getopt reports -? as an error, as it reports unknown options with
    // ?; W; would make -W a long option.
    if(c == ';')
      return {};
    const bool argument = i + 1 < letters.size() && letters[i + 1] == ':';
    if(c != '?')
      options.emplace_back(std::string{"-"} + c, argument);
    while(i + 1 < letters.size() && letters[i + 1] == ':')
      ++i;
  }
  std::size_t from = 0;
  while(from < longopts.size())
  {
    const auto comma = longopts.find(',', from);
    std::string name = longopts.substr(
      from, comma == std::string::npos ? std::string::npos : comma - from);
    from = comma == std::string::npos ? longopts.size() : comma + 1;
    const bool argument = !name.empty() && name.back() == ':';
    while(!name.empty() && name.back() == ':')
      name.pop_back();
    if(!name.empty())
      options.emplace_back("--" + name, argument);
  }

  // getopt outputs the options that it parsed also where some were not
  // valid, with status 1; it outputs nothing on its own usage errors.
  // Where GETOPT_COMPATIBLE is in the environment, -o is the option
  // string instead, and the output is not quoted. The variable is then
  // set (exported or not).
  const exprt succeeded = or_exprt{
    equal_exprt{substitution_status(), from_integer(0, status_type())},
    equal_exprt{substitution_status(), from_integer(1, status_type())}};
  const exprt compatible = is_set(variable("GETOPT_COMPATIBLE", loc));
  const exprt supported = implies_exprt{succeeded, not_exprt{compatible}};
  check(
    dest,
    supported,
    "shell-unsupported",
    "GETOPT_COMPATIBLE is not set where getopt outputs options (the "
    "output of getopt in its compatible mode is not modelled)",
    loc);
  add(dest, code_assumet{supported}, loc);
  // At most max_arguments words, as the fields of a list.
  const symbol_exprt overflow = temporary(bool_typet{}, dest, loc);
  assign(dest, overflow, nondet(bool_typet{}, loc), loc);
  const exprt bounded = implies_exprt{succeeded, not_exprt{overflow}};
  check(
    dest,
    bounded,
    "shell-argument-count",
    "the output of getopt has at most --shell-max-arguments (" +
      std::to_string(max_arguments) + ") words",
    loc);
  add(dest, code_assumet{bounded}, loc);

  // The output, made of its words rather than constrained to their
  // language, which the solvers decide much faster: an option, --, or (as
  // the argument of an option, or a parameter after --) a quoted string.
  // The words are the ghost list of fields of the last output of getopt
  // (see getopt_output_fields), as finding them in the output took cvc5
  // more than 30 s with three words.
  const field_listt fields = getopt_output_fields();
  const symbol_exprt count = temporary(integer_type(), dest, loc);
  assign(dest, count, nondet(integer_type(), loc), loc);
  const exprt zero = from_integer(0, integer_type());
  exprt::operandst constraints{
    binary_relation_exprt{count, ID_ge, zero},
    binary_relation_exprt{
      count, ID_le, from_integer(max_arguments, integer_type())}};
  // The states: before options (0), before the argument of an option (1),
  // and after -- (2).
  const auto state_value = [this](int n) -> exprt
  { return from_integer(n, integer_type()); };
  exprt state = state_value(0);
  const std::size_t dash_dash = options.size();
  const std::size_t quoted = options.size() + 1;
  const auto kind_is = [this](const exprt &kind, std::size_t n) -> exprt {
    return equal_exprt{kind, from_integer(n, integer_type())};
  };
  const symbol_exprt text = temporary(payload_type(), dest, loc);
  assign(dest, text, payload_literal(""), loc);
  for(std::size_t i = 0;; ++i)
  {
    const exprt at = from_integer(i, integer_type());
    const symbol_exprt current = temporary(integer_type(), dest, loc);
    assign(dest, current, state, loc);
    // The output ends after --.
    constraints.push_back(implies_exprt{
      equal_exprt{count, at}, equal_exprt{current, state_value(2)}});
    if(i == max_arguments)
      break;
    const symbol_exprt kind = temporary(integer_type(), dest, loc);
    assign(dest, kind, nondet(integer_type(), loc), loc);
    const exprt present = binary_relation_exprt{at, ID_lt, count};
    constraints.push_back(implies_exprt{
      present,
      if_exprt{
        equal_exprt{current, state_value(0)},
        binary_relation_exprt{
          kind, ID_le, from_integer(dash_dash, integer_type())},
        kind_is(kind, quoted)}});
    // The next state.
    exprt next =
      if_exprt{kind_is(kind, dash_dash), state_value(2), state_value(0)};
    for(std::size_t j = 0; j < options.size(); ++j)
    {
      if(options[j].second)
        next = if_exprt{kind_is(kind, j), state_value(1), next};
    }
    state = if_exprt{
      equal_exprt{current, state_value(0)},
      next,
      if_exprt{
        equal_exprt{current, state_value(1)}, state_value(0), state_value(2)}};
    // The field, and its word; assigned in branches, which the string
    // refinement solver needs rather than conditional expressions.
    const symbol_exprt value = temporary(payload_type(), dest, loc);
    assign(dest, value, payload_literal("--"), loc);
    for(std::size_t j = 0; j < options.size(); ++j)
    {
      code_blockt option;
      assign(option, value, payload_literal(options[j].first), loc);
      add(dest, code_ifthenelset{kind_is(kind, j), std::move(option)}, loc);
    }
    {
      code_blockt argument;
      assign(argument, value, payload_of(nondet_string(argument, loc)), loc);
      add(
        dest,
        code_ifthenelset{kind_is(kind, quoted), std::move(argument)},
        loc);
    }
    assign(
      dest,
      index_exprt{fields.array, from_integer(i, index_type())},
      make_value(value),
      loc);
    if(!native())
      continue;
    const symbol_exprt word = temporary(payload_type(), dest, loc);
    assign(dest, word, value, loc);
    {
      const exprt quote = payload_literal("'");
      code_blockt argument;
      assign(
        argument,
        word,
        payload_concat(
          payload_concat(
            quote,
            payload_replace_all(value, "'", "'\\''", argument, loc),
            argument,
            loc),
          quote,
          argument,
          loc),
        loc);
      add(
        dest,
        code_ifthenelset{kind_is(kind, quoted), std::move(argument)},
        loc);
    }
    code_blockt append;
    assign(
      append,
      text,
      payload_concat(
        text,
        payload_concat(payload_literal(" "), word, append, loc),
        append,
        loc),
      loc);
    add(dest, code_ifthenelset{present, std::move(append)}, loc);
  }
  // With refined strings, the output is any string: making it of its
  // words did not finish in 120 s for two words, nor for eight without
  // the quoting of arguments. Its fields stay those of the words.
  if(!native())
    assign(dest, text, payload_of(nondet_string(dest, loc)), loc);
  // busybox reports its own usage errors with status 1, and outputs
  // nothing then (as where it lacks long options).
  const symbol_exprt empty = temporary(bool_typet{}, dest, loc);
  assign(
    dest,
    empty,
    and_exprt{
      nondet(bool_typet{}, loc),
      equal_exprt{substitution_status(), from_integer(1, status_type())}},
    loc);
  add(
    dest,
    code_assumet{implies_exprt{
      and_exprt{succeeded, not_exprt{empty}}, conjunction(constraints)}},
    loc);
  assign(
    dest,
    fields.count,
    typecast_exprt::conditional_cast(
      if_exprt{empty, zero, exprt{count}}, fields.count.type()),
    loc);
  {
    code_blockt nothing;
    assign(nothing, text, payload_literal(""), loc);
    add(dest, code_ifthenelset{empty, std::move(nothing)}, loc);
  }
  assign(dest, getopt_output_valid(), succeeded, loc);
  assign(dest, getopt_output_text(), text, loc);
  // Otherwise, the output is not modelled.
  const symbol_exprt value = temporary(string_type(), dest, loc);
  code_blockt output;
  assign(output, value, make_value(text), loc);
  code_blockt other;
  assign(other, value, nondet_string(other, loc), loc);
  add(
    dest,
    code_ifthenelset{succeeded, std::move(output), std::move(other)},
    loc);
  return exprt{value};
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
