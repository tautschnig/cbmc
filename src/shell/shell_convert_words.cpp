/// \file
/// Shell converter: word expansion (POSIX.1-2024 XCU 2.6).

#include "shell_convert_class.h"

// ------------------------------------------------------------------
// Words

exprt shell_convertert::expand_word(
  const shell_wordt &word,
  shell_word_contextt context,
  code_blockt &dest,
  const source_locationt &loc)
{
  if(context == shell_word_contextt::IGNORED)
  {
    // Evaluate only the parts with side effects, for those effects.
    for(const auto &part : word)
    {
      if(has_side_effects(part))
        expand_part(part, context, false, false, dest);
    }
    return string_literal("", loc);
  }
  const exprt value = expand_parts(word, context, false, true, dest);
  if(context == shell_word_contextt::FIELD && may_vanish(word))
  {
    // A word of only unquoted expansions that are empty yields no field
    // (XCU 2.6.5), which the caller would need to model.
    check(
      dest,
      not_exprt{is_empty(value)},
      "shell-unsupported",
      "the unquoted " + display(word) +
        " is not empty (an empty one yields no field, which is not "
        "supported here)",
      loc);
  }
  return value;
}

/// Whether \p word may yield no field: it has no quoted parts and no
/// literal text, so that it is empty if its expansions are.
bool shell_convertert::may_vanish(const shell_wordt &word)
{
  using kindt = shell_word_partt::kindt;
  for(const auto &part : word)
  {
    if(
      part.kind == kindt::QUOTED_LITERAL || part.kind == kindt::DOUBLE_QUOTED ||
      (part.kind == kindt::LITERAL && !part.text.empty()) ||
      part.kind == kindt::ARITHMETIC ||
      (part.kind == kindt::PARAMETER &&
       (part.text == "?" || part.text == "#") &&
       part.parameter_operator.empty()))
    {
      return false;
    }
  }
  return true;
}

exprt shell_convertert::expand_parts(
  const std::vector<shell_word_partt> &parts,
  shell_word_contextt context,
  bool quoted,
  bool word_start,
  code_blockt &dest)
{
  std::optional<exprt> result;
  std::string pending_text;
  source_locationt pending_loc;
  bool first = word_start;

  auto flush_text = [&]()
  {
    if(pending_text.empty() && result.has_value())
      return;
    exprt literal = string_literal(pending_text, pending_loc);
    result = result.has_value() ? concat(*result, literal, dest, pending_loc)
                                : literal;
    pending_text.clear();
  };

  for(std::size_t index = 0; index < parts.size(); ++index)
  {
    const shell_word_partt &part = parts[index];
    using kindt = shell_word_partt::kindt;
    if(
      part.kind == kindt::LITERAL && !quoted && first &&
      context != shell_word_contextt::IGNORED && !part.text.empty() &&
      part.text[0] == '~')
    {
      // Tilde expansion (XCU 2.6.1) of ~ alone, or before a slash: the
      // value of HOME, where it is set (where it is not, shells differ).
      const std::size_t slash = part.text.find('/');
      const bool alone = part.text.size() == 1 && index + 1 == parts.size();
      if(slash != 1 && !alone)
        unsupported(part.location, "tilde expansion (XCU 2.6.1)");
      if(!pending_text.empty())
        flush_text();
      const symbol_exprt home = variable("HOME", part.location);
      check(
        dest,
        is_set(home),
        "shell-unsupported",
        "HOME is set where ~ is expanded (shells differ where it is not)",
        part.location);
      const exprt value = value_of(home, part.location);
      result = result.has_value() ? concat(*result, value, dest, part.location)
                                  : value;
      shell_word_partt rest = part;
      rest.text = part.text.substr(1);
      pending_loc = part.location;
      pending_text += unquoted_literal(rest, context, false, dest);
      first = false;
      continue;
    }
    if(part.kind == kindt::LITERAL || part.kind == kindt::QUOTED_LITERAL)
    {
      if(pending_text.empty())
        pending_loc = part.location;
      pending_text += part.kind == kindt::QUOTED_LITERAL ? part.text
                      : quoted ? double_quoted_literal(part.text)
                               : unquoted_literal(part, context, first, dest);
    }
    else
    {
      if(!pending_text.empty())
        flush_text();
      exprt value = expand_part(part, context, quoted, first, dest);
      result = result.has_value() ? concat(*result, value, dest, part.location)
                                  : value;
    }
    first = false;
  }
  if(!pending_text.empty() || !result.has_value())
    flush_text();
  return *result;
}

exprt shell_convertert::expand_part(
  const shell_word_partt &part,
  shell_word_contextt context,
  bool quoted,
  bool word_start,
  code_blockt &dest)
{
  using kindt = shell_word_partt::kindt;
  // Arithmetic expansion, $?, and $# yield a non-empty decimal number,
  // which field splitting with the default IFS cannot split; the script
  // cannot change IFS (check_parameter_name).
  const bool single_field =
    part.kind == kindt::ARITHMETIC ||
    (part.kind == kindt::PARAMETER && (part.text == "?" || part.text == "#") &&
     part.parameter_operator.empty());
  if(
    !quoted && context == shell_word_contextt::FIELD &&
    part.kind != kindt::LITERAL && part.kind != kindt::QUOTED_LITERAL &&
    part.kind != kindt::DOUBLE_QUOTED && !single_field)
  {
    // Field splitting with the default IFS (the script cannot change it,
    // see variable()) and pathname expansion leave a value without
    // blanks and pattern characters as it is; other values are reported.
    // zsh only splits command substitutions (see split_known_parameter).
    const std::string ifs =
      program.dialect == shell_dialectt::ZSH && part.kind == kindt::PARAMETER
        ? std::string{}
        : ifs_for_splitting(dest, part.location);
    if(const auto literal = known_literal(part))
    {
      if(!ifs.empty() && literal->find_first_of(ifs) != std::string::npos)
      {
        check(
          dest,
          false_exprt{},
          "shell-unsupported",
          "the value of the unquoted " + display({part}) +
            " has no byte of IFS (field splitting, XCU 2.6.5, is not "
            "supported here)",
          part.location);
      }
      const exprt value = string_literal(*literal, part.location);
      if(literal->find_first_of("*?[") != std::string::npos)
        check_no_pattern_characters(value, part, dest);
      return value;
    }
    const exprt value =
      expand_part(part, shell_word_contextt::CASE_SUBJECT, false, false, dest);
    if(!ifs.empty())
    {
      check(
        dest,
        not_exprt{payload_contains_byte(
          payload_of(value), ifs_set(ifs), dest, part.location)},
        "shell-unsupported",
        "the value of the unquoted " + display({part}) +
          " has no byte of IFS (field splitting, XCU 2.6.5, is not "
          "supported here)",
        part.location);
    }
    check_no_pattern_characters(value, part, dest);
    return value;
  }
  switch(part.kind)
  {
  case kindt::LITERAL:
  case kindt::QUOTED_LITERAL:
    return expand_parts({part}, context, quoted, word_start, dest);
  case kindt::DOUBLE_QUOTED:
    return expand_parts(part.parts, context, true, false, dest);
  case kindt::PARAMETER:
    return expand_parameter(part, context, quoted, dest);
  case kindt::COMMAND_SUBSTITUTION:
    return expand_command_substitution(part, dest);
  case kindt::ARITHMETIC:
    return expand_arithmetic(part, dest);
  }
  UNREACHABLE;
}

void shell_convertert::check_no_pattern_characters(
  const exprt &value,
  const shell_word_partt &part,
  code_blockt &dest)
{
  // zsh does not apply pathname expansion to the results of expansions
  // (unless GLOB_SUBST is set, which setopt, which is not supported,
  // would do).
  if(program.dialect == shell_dialectt::ZSH)
    return;
  std::bitset<256> pattern;
  for(unsigned char c : {'*', '?', '['})
    pattern.set(c);
  exprt no_pattern = not_exprt{
    payload_contains_byte(payload_of(value), pattern, dest, part.location)};
  if(option_possible("noglob"))
    no_pattern = or_exprt{option("noglob"), no_pattern};
  check(
    dest,
    no_pattern,
    "shell-unsupported",
    "the value of the unquoted " + display({part}) +
      " has no *, ? or [ (pathname expansion, XCU 2.6.6, is not supported)",
    part.location);
}

/// Quote removal for unquoted text (XCU 2.2.1, 2.6.7), rejecting text
/// that tilde expansion (XCU 2.6.1) or pathname expansion (XCU 2.6.6)
/// would change.
std::string shell_convertert::unquoted_literal(
  const shell_word_partt &part,
  shell_word_contextt context,
  bool word_start,
  code_blockt &dest)
{
  const std::string &text = part.text;
  const bool globbing = context == shell_word_contextt::FIELD;
  std::string result;
  for(std::size_t i = 0; i < text.size(); ++i)
  {
    const char c = text[i];
    const bool tilde_position =
      c == '~' &&
      ((i == 0 && word_start && context != shell_word_contextt::IGNORED) ||
       (context == shell_word_contextt::ASSIGNMENT && i > 0 &&
        text[i - 1] == ':'));
    if(tilde_position)
      unsupported(part.location, "tilde expansion (XCU 2.6.1)");
    if(globbing && (c == '*' || c == '?' || c == '['))
    {
      if(!option_possible("noglob"))
      {
        unsupported(
          part.location,
          "unquoted pattern character '" + std::string{c} +
            "' (pathname expansion, XCU 2.6.6); quote it");
      }
      // With set -f the character is literal; report it otherwise.
      check(
        dest,
        option("noglob"),
        "shell-unsupported",
        "noglob (set -f) is on where pathname expansion would apply to '" +
          std::string{c} + "' (pathname expansion is not supported)",
        part.location);
    }
    if(c == '\\' && i + 1 < text.size())
    {
      ++i;
      if(text[i] != '\n')
        result += text[i];
    }
    else
      result += c;
  }
  return result;
}

/// Inside double quotes a backslash only escapes $ ` " \ and newline
/// (XCU 2.2.3).
std::string shell_convertert::double_quoted_literal(const std::string &text)
{
  std::string result;
  for(std::size_t i = 0; i < text.size(); ++i)
  {
    const char c = text[i];
    if(c == '\\' && i + 1 < text.size())
    {
      const char next = text[i + 1];
      if(next == '$' || next == '`' || next == '"' || next == '\\')
      {
        result += next;
        ++i;
        continue;
      }
      if(next == '\n')
      {
        ++i;
        continue;
      }
    }
    result += c;
  }
  return result;
}

/// The symbol holding the parameter named in \p part.
exprt shell_convertert::parameter(const shell_word_partt &part)
{
  const std::string &name = part.text;
  if(!name.empty() && std::isdigit(static_cast<unsigned char>(name[0])))
  {
    // Leading zeros are allowed, and the number can be large.
    const std::size_t first = name.find_first_not_of('0');
    if(first == std::string::npos)
      return special("0");
    if(name.size() - first > 9)
      return unset_string(part.location);
    return positional_parameter(std::stoul(name.substr(first)), part.location);
  }
  if(name == "$" || name == "-" || name == "!")
    return special(name);
  return variable(name, part.location);
}

exprt shell_convertert::expand_parameter(
  const shell_word_partt &part,
  shell_word_contextt context,
  bool quoted,
  code_blockt &dest)
{
  const source_locationt &loc = part.location;
  exprt value;
  bool assignable = false;
  if(part.text == "?")
    value = from_integer_expr(status(), dest, loc);
  else if(part.text == "#")
    value = from_integer_expr(argument_count(), dest, loc);
  else if(
    (part.text == "*" &&
     (part.parameter_length || !part.parameter_operator.empty())) ||
    (part.text == "@" && part.parameter_length &&
     part.parameter_operator.empty()))
  {
    // ${#@} is ${#*} in all shells.
    return expand_star_operator(part, context, quoted, dest);
  }
  else if(part.text == "@" || part.text == "*")
  {
    if(part.parameter_length || !part.parameter_operator.empty())
      unsupported(loc, "${" + part.text + "} with an operator");
    // "$@" as a whole word is expanded by expand_fields; elsewhere, its
    // result is unspecified unless it is subject to field splitting. In
    // assignments, bash and mksh join the fields with spaces, and dash,
    // busybox and zsh with the first byte of IFS, as for $*: the same
    // where that is a space.
    if(part.text == "@")
    {
      if(context != shell_word_contextt::ASSIGNMENT)
        unsupported(loc, "$@ where it does not expand to a list of fields");
      const auto ifs = known_ifs();
      if(
        ifs.has_value() && (ifs->empty() || (*ifs)[0] != ' ') &&
        program.dialect != shell_dialectt::BASH &&
        program.dialect != shell_dialectt::MKSH)
      {
        unsupported(loc, "$@ in an assignment where IFS starts with no space");
      }
    }
    if(
      part.text == "@" && program.dialect != shell_dialectt::BASH &&
      program.dialect != shell_dialectt::MKSH)
    {
      return join_positional_parameters(dest, loc);
    }
    if(part.text == "@")
      return join_with_spaces(dest, loc);
    return join_positional_parameters(dest, loc);
  }
  else
  {
    if(part.text == "IFS")
    {
      const std::string &op = part.parameter_operator;
      if(!op.empty() && op.back() == '=')
        unsupported(loc, "assigning to IFS in an expansion");
      // The value is a literal, which assignments keep track of; the
      // operators below apply to it, or to an unset IFS.
      const auto literal = ifs_value(dest, loc);
      if(literal.has_value() && op.empty() && !part.parameter_length)
        return string_literal(*literal, loc);
      value =
        literal.has_value() ? string_literal(*literal, loc) : unset_string(loc);
      if(op.empty() || op[0] == '%' || op[0] == '#')
        check_nounset(value, part.text, dest, loc);
    }
    else if(const auto literal = known_literal(part))
    {
      check_parameter_name(part.text, loc);
      return string_literal(*literal, loc);
    }
    else
    {
      value = parameter(part);
      assignable = value.id() == ID_symbol &&
                   id2string(to_symbol_expr(value).get_identifier())
                       .rfind("shell::var::", 0) == 0;
      const std::string &op = part.parameter_operator;
      if(op.empty() || op[0] == '%' || op[0] == '#')
        check_nounset(value, part.text, dest, loc);
    }
  }

  if(part.parameter_length)
  {
    if(!part.parameter_operator.empty())
      unsupported(loc, "${#parameter} with an operator");
    return from_integer_expr(string_length(value, dest, loc), dest, loc);
  }
  if(part.parameter_operator.empty())
    return value_of(value, loc);
  return expand_parameter_operator(
    part, value, assignable, context, quoted, dest);
}

/// ${#*}, and ${*<op>word} for -, +, :- and :+. The shells differ: dash
/// and busybox take the length of "$*", bash, mksh and zsh $#; for - and
/// +, * is set in dash, busybox and zsh, and in bash and mksh only with
/// positional parameters. With a colon, all agree that "$*" is used. The
/// POSIX dialect, which also stands for bash run as sh, allows either.
exprt shell_convertert::expand_star_operator(
  const shell_word_partt &part,
  shell_word_contextt context,
  bool quoted,
  code_blockt &dest)
{
  const source_locationt &loc = part.location;
  const std::string &op = part.parameter_operator;
  const exprt joined = join_positional_parameters(dest, loc);
  const exprt some = binary_relation_exprt{
    argument_count(), ID_gt, from_integer(0, count_type())};
  const bool dash_like = program.dialect == shell_dialectt::POSIX;
  const bool bash_like = program.dialect == shell_dialectt::BASH ||
                         program.dialect == shell_dialectt::MKSH ||
                         program.dialect == shell_dialectt::POSIX;
  if(part.parameter_length)
  {
    if(!op.empty())
      unsupported(loc, "${#parameter} with an operator");
    const exprt length =
      from_integer_expr(string_length(joined, dest, loc), dest, loc);
    const exprt count = from_integer_expr(argument_count(), dest, loc);
    const symbol_exprt result = temporary(string_type(), dest, loc);
    if(program.dialect == shell_dialectt::POSIX)
    {
      code_blockt use_count, use_length;
      assign(use_count, result, count, loc);
      assign(use_length, result, length, loc);
      add(
        dest,
        code_ifthenelset{
          nondet(bool_typet{}, loc),
          std::move(use_count),
          std::move(use_length)},
        loc);
    }
    else
      assign(dest, result, count, loc);
    return result;
  }
  if(op != "-" && op != "+" && op != ":-" && op != ":+")
    unsupported(loc, "${*} with the operator " + op);
  // * as a parameter: its value is "$*", and whether it is set differs.
  const symbol_exprt parameter = temporary(string_type(), dest, loc);
  assign(dest, parameter, joined, loc);
  if(op[0] != ':' && bash_like)
  {
    exprt unset = not_exprt{some};
    if(dash_like)
      unset = and_exprt{nondet(bool_typet{}, loc), unset};
    code_blockt make_unset;
    assign(make_unset, parameter, unset_string(loc), loc);
    add(dest, code_ifthenelset{unset, std::move(make_unset)}, loc);
  }
  return expand_parameter_operator(
    part, parameter, false, context, quoted, dest);
}

/// ${parameter<op>word} for the operators of XCU 2.6.2 except pattern
/// removal.
exprt shell_convertert::expand_parameter_operator(
  const shell_word_partt &part,
  const exprt &parameter,
  bool assignable,
  shell_word_contextt context,
  bool quoted,
  code_blockt &dest)
{
  const source_locationt &loc = part.location;
  const std::string &op = part.parameter_operator;
  if(op == "%" || op == "%%" || op == "#" || op == "##")
    return remove_pattern(value_of(parameter, loc), op, part.parts, dest, loc);

  const bool colon = op[0] == ':';
  const char kind = op.back();
  // Whether the parameter counts as unset (or null, with a colon).
  const exprt unset =
    colon ? exprt{or_exprt{not_exprt{is_set(parameter)}, is_empty(parameter)}}
          : exprt{not_exprt{is_set(parameter)}};

  const symbol_exprt result = temporary(string_type(), dest, loc);
  code_blockt use_word;
  const shell_word_contextt word_context =
    context == shell_word_contextt::IGNORED ? shell_word_contextt::ASSIGNMENT
                                            : context;
  exprt word = expand_parts(part.parts, word_context, quoted, false, use_word);
  code_blockt use_value;
  assign(use_value, result, value_of(parameter, loc), loc);

  if(kind == '-' || kind == '=')
  {
    if(kind == '=')
    {
      if(!assignable)
        unsupported(loc, "assigning to a special or positional parameter");
      // Expansion results are always set.
      assign(use_word, parameter, word, loc);
      known_literals.erase(part.text);
    }
    assign(use_word, result, word, loc);
    add(
      dest,
      code_ifthenelset{unset, std::move(use_word), std::move(use_value)},
      loc);
  }
  else if(kind == '+')
  {
    assign(use_word, result, word, loc);
    code_blockt use_empty;
    assign(use_empty, result, string_literal("", loc), loc);
    add(
      dest,
      code_ifthenelset{unset, std::move(use_empty), std::move(use_word)},
      loc);
  }
  else
  {
    // '?': write the word as a message and exit, as non-interactive
    // shells do; the status differs between shells.
    nondet_status(use_word, 255, loc);
    add(
      use_word,
      code_assumet{notequal_exprt{status(), from_integer(0, status_type())}},
      loc);
    add(use_word, code_frontend_returnt{}, loc);
    add(
      dest,
      code_ifthenelset{unset, std::move(use_word), std::move(use_value)},
      loc);
  }
  return result;
}

bool shell_convertert::has_side_effects(const shell_word_partt &part) const
{
  using kindt = shell_word_partt::kindt;
  switch(part.kind)
  {
  case kindt::LITERAL:
  case kindt::QUOTED_LITERAL:
    return false;
  case kindt::ARITHMETIC:
    // Assignments, and errors such as division by zero.
  case kindt::COMMAND_SUBSTITUTION:
    // The commands' properties, and the status of the substitution.
    return true;
  case kindt::DOUBLE_QUOTED:
    for(const auto &p : part.parts)
    {
      if(has_side_effects(p))
        return true;
    }
    return false;
  case kindt::PARAMETER:
  {
    const std::string &op = part.parameter_operator;
    if(!op.empty() && (op.back() == '=' || op.back() == '?'))
      return true;
    // With -u, expanding an unset parameter makes the shell exit.
    if(op.empty() && nounset_possible && part.text != "@" && part.text != "*")
    {
      return true;
    }
    for(const auto &p : part.parts)
    {
      if(has_side_effects(p))
        return true;
    }
    return false;
  }
  }
  UNREACHABLE;
}

bool shell_convertert::has_state_side_effects(const shell_wordt &word) const
{
  using kindt = shell_word_partt::kindt;
  for(const auto &part : word)
  {
    if(part.kind == kindt::COMMAND_SUBSTITUTION)
      continue;
    if(part.kind == kindt::DOUBLE_QUOTED)
    {
      if(has_state_side_effects(part.parts))
        return true;
      continue;
    }
    if(has_side_effects(part))
    {
      // A parameter expansion whose word has the side effects.
      if(
        part.kind == kindt::PARAMETER && !nounset_possible &&
        (part.parameter_operator.empty() ||
         (part.parameter_operator.back() != '=' &&
          part.parameter_operator.back() != '?')) &&
        !has_state_side_effects(part.parts))
      {
        continue;
      }
      return true;
    }
  }
  return false;
}

bool shell_convertert::has_side_effects(const shell_wordt &word) const
{
  for(const auto &part : word)
  {
    if(has_side_effects(part))
      return true;
  }
  return false;
}

std::optional<std::string>
shell_convertert::static_text(const shell_wordt &word, bool quoted)
{
  using kindt = shell_word_partt::kindt;
  std::string result;
  for(const auto &part : word)
  {
    if(part.kind == kindt::QUOTED_LITERAL)
      result += part.text;
    else if(part.kind == kindt::LITERAL && quoted)
      result += double_quoted_literal(part.text);
    else if(part.kind == kindt::LITERAL)
    {
      for(std::size_t i = 0; i < part.text.size(); ++i)
      {
        if(part.text[i] == '\\' && i + 1 < part.text.size())
          ++i;
        result += part.text[i];
      }
    }
    else if(part.kind == kindt::DOUBLE_QUOTED)
    {
      auto inner = static_text(part.parts, true);
      if(!inner.has_value())
        return {};
      result += *inner;
    }
    else
      return {};
  }
  return result;
}

std::string shell_convertert::display(const shell_wordt &parts)
{
  using kindt = shell_word_partt::kindt;
  std::string result;
  for(const auto &part : parts)
  {
    switch(part.kind)
    {
    case kindt::LITERAL:
      result += part.text;
      break;
    case kindt::QUOTED_LITERAL:
      result += "'" + part.text + "'";
      break;
    case kindt::DOUBLE_QUOTED:
      result += "\"" + display(part.parts) + "\"";
      break;
    case kindt::PARAMETER:
      result += "${" + std::string{part.parameter_length ? "#" : ""} +
                part.text + part.parameter_operator + display(part.parts) + "}";
      break;
    case kindt::COMMAND_SUBSTITUTION:
      result += "$(...)";
      break;
    case kindt::ARITHMETIC:
      result += "$((" + part.text + "))";
      break;
    }
  }
  return result;
}
