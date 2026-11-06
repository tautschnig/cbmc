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
  if(indirect_name(part.text).has_value())
    return expand_indirect_parameter(part, context, quoted, dest);
  if(arrays.count(part.text) != 0)
    return expand_array_parameter(part, context, quoted, dest);
  if(!part.index.empty())
    unsupported(
      part.location, "array subscript of a name not seen as an array");
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
    else if(part.text == "RANDOM")
    {
      // A new number from 0 to 32767 at each expansion, except in dash,
      // where RANDOM is an ordinary variable: the POSIX dialect allows
      // any value, set or unset.
      if(
        !part.parameter_operator.empty() &&
        part.parameter_operator.back() == '=')
        unsupported(loc, "assigning to RANDOM in an expansion");
      if(program.dialect == shell_dialectt::POSIX)
        value = environment_string(dest, loc);
      else
      {
        const symbol_exprt number = temporary(integer_type(), dest, loc);
        assign(dest, number, nondet(integer_type(), loc), loc);
        add(
          dest,
          code_assumet{and_exprt{
            binary_relation_exprt{
              number, ID_ge, from_integer(0, integer_type())},
            binary_relation_exprt{
              number, ID_le, from_integer(32767, integer_type())}}},
          loc);
        value = from_integer_expr(number, dest, loc);
      }
    }
    else if(const auto literal = known_literal(part))
    {
      check_parameter_name(part.text, loc);
      return string_literal(*literal, loc);
    }
    else if(
      is_bash_readonly_id(part.text) &&
      (program.dialect == shell_dialectt::BASH ||
       program.dialect == shell_dialectt::POSIX))
    {
      // Read-only (in bash): not assignable here.
      value = bash_readonly_id(part.text);
    }
    else
    {
      value = parameter(part);
      assignable = value.id() == ID_symbol &&
                   id2string(to_symbol_expr(value).get_identifier())
                       .rfind("shell::var::", 0) == 0;
      const std::string &op = part.parameter_operator;
      if(
        op.empty() || op[0] == '%' || op[0] == '#' || op[0] == '/' || op == ":")
      {
        check_nounset(value, part.text, dest, loc);
      }
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
  if(op[0] == '/')
    return substitute_pattern(part, value_of(parameter, loc), dest);
  if(op == ":")
    return substring_expansion(part, value_of(parameter, loc), dest);

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
    // With -u, expanding an unset parameter makes the shell exit; the
    // operators are those for which expand_part checks it.
    if(
      (op.empty() || op[0] == '%' || op[0] == '#' || op[0] == '/' ||
       op == ":") &&
      nounset_possible && part.text != "@" && part.text != "*")
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

/// ${parameter/pattern/string} and its forms //, /# and /% (bash, mksh,
/// zsh): the first, every, a leading or a trailing longest match of the
/// pattern is replaced. /# and /% take any pattern; / and // only
/// patterns without pattern characters.
exprt shell_convertert::substitute_pattern(
  const shell_word_partt &part,
  const exprt &value,
  code_blockt &dest)
{
  const source_locationt &loc = part.location;
  const std::string &op = part.parameter_operator;
  // zsh reads quotes in the pattern differently.
  if(program.dialect == shell_dialectt::ZSH)
    unsupported(loc, "pattern substitution in zsh");
  // The string. bash 5.2 replaces an unquoted & with the match, and
  // treats backslashes in it differently from mksh and zsh.
  for(const auto &p : part.replacement)
  {
    if(
      (p.kind == shell_word_partt::kindt::LITERAL ||
       p.kind == shell_word_partt::kindt::QUOTED_LITERAL) &&
      p.text.find_first_of("&\\") != std::string::npos)
    {
      unsupported(loc, "pattern substitution with & or \\ in the string");
    }
  }
  const exprt replacement = expand_parts(
    part.replacement, shell_word_contextt::ASSIGNMENT, true, false, dest);
  {
    std::bitset<256> special;
    special.set('&');
    special.set('\\');
    check(
      dest,
      not_exprt{
        payload_contains_byte(payload_of(replacement), special, dest, loc)},
      "shell-unsupported",
      "the string of " + display({part}) +
        " has no & or \\ (in which shells differ)",
      loc);
  }
  const exprt payload = payload_of(value);
  const symbol_exprt result = temporary(string_type(), dest, loc);
  if(op == "/#" || op == "/%")
  {
    // The rest after removing the longest match, if there is one.
    const exprt rest =
      remove_pattern(value, op == "/#" ? "##" : "%%", part.parts, dest, loc);
    const exprt empty_match =
      pattern_matches(part.parts, string_literal("", loc), dest, loc);
    const exprt matched =
      or_exprt{not_exprt{equal(rest, value, dest, loc)}, empty_match};
    code_blockt replace;
    assign(
      replace,
      result,
      op == "/#" ? concat(replacement, rest, replace, loc)
                 : concat(rest, replacement, replace, loc),
      loc);
    code_blockt keep;
    assign(keep, result, value, loc);
    add(
      dest,
      code_ifthenelset{matched, std::move(replace), std::move(keep)},
      loc);
    return result;
  }
  // / and //: a literal pattern.
  const shell_wordt prepared = prepare_pattern(part.parts, dest, loc);
  const shell_patternt pattern = compile_pattern(prepared, loc);
  if(!pattern.is_literal())
    unsupported(
      loc,
      "pattern substitution ${x" + op +
        "pattern/string} with "
        "pattern characters");
  const exprt needle = payload_of(literal_pattern_string(pattern, dest, loc));
  const exprt zero = from_integer(0, integer_type());
  const exprt needle_length = payload_length(needle, dest, loc);
  assign(dest, result, value, loc);
  if(native())
  {
    // SMT-LIB's str.replace and str.replace_all, where the pattern is not
    // empty (it then matches nothing).
    code_blockt replace;
    assign(
      replace,
      result,
      make_value(string_function(
        op == "//" ? ID_cprover_string_replace_all_func
                   : ID_cprover_string_replace_func,
        {payload, needle, payload_of(replacement)},
        payload_type(),
        replace,
        loc)),
      loc);
    add(
      dest,
      code_ifthenelset{
        binary_relation_exprt{needle_length, ID_gt, zero}, std::move(replace)},
      loc);
    return result;
  }
  // An empty pattern matches nothing. Each round replaces the first match
  // at or after a position; as strings have at most max_length bytes, so
  // many rounds replace every match.
  const std::size_t rounds = op == "//" ? max_length : 1;
  const symbol_exprt from = temporary(integer_type(), dest, loc);
  assign(dest, from, zero, loc);
  const symbol_exprt more = temporary(bool_typet{}, dest, loc);
  assign(dest, more, binary_relation_exprt{needle_length, ID_gt, zero}, loc);
  for(std::size_t k = 0; k < rounds; ++k)
  {
    code_blockt round;
    const exprt current = payload_of(result);
    const symbol_exprt found = temporary(integer_type(), round, loc);
    assign(
      round, found, payload_index_of(current, needle, from, round, loc), loc);
    code_blockt replace;
    const exprt length = payload_length(current, replace, loc);
    const exprt before =
      make_value(payload_substring(current, zero, found, replace, loc));
    const exprt after = make_value(payload_substring(
      current, plus_exprt{found, needle_length}, length, replace, loc));
    const exprt joined =
      concat(concat(before, replacement, replace, loc), after, replace, loc);
    assign(
      replace,
      from,
      plus_exprt{found, string_length(replacement, replace, loc)},
      loc);
    assign(replace, result, joined, loc);
    code_blockt stop;
    assign(stop, more, false_exprt{}, loc);
    add(
      round,
      code_ifthenelset{
        binary_relation_exprt{found, ID_ge, zero},
        std::move(replace),
        std::move(stop)},
      loc);
    add(dest, code_ifthenelset{more, std::move(round)}, loc);
  }
  return result;
}

/// ${parameter:offset} and ${parameter:offset:length} (bash, mksh, zsh)
/// with an offset and a length that are not negative (where they are,
/// mksh differs).
exprt shell_convertert::substring_expansion(
  const shell_word_partt &part,
  const exprt &value,
  code_blockt &dest)
{
  const source_locationt &loc = part.location;
  const auto operand = [&](const shell_wordt &word)
  {
    const auto text = static_text(word);
    if(!text.has_value())
      unsupported(loc, "substring expansion operand that is an expansion");
    const auto parsed = parse_shell_arithmetic(*text, arith_options);
    if(!parsed.expression.has_value())
      unsupported(loc, "substring expansion operand that is not valid");
    const symbol_exprt number = temporary(integer_type(), dest, loc);
    assign(
      dest,
      number,
      typecast_exprt::conditional_cast(
        arith(*parsed.expression, dest, loc), integer_type()),
      loc);
    check(
      dest,
      binary_relation_exprt{number, ID_ge, from_integer(0, integer_type())},
      "shell-unsupported",
      "the operand " + *text +
        " of the substring expansion is not negative (shells differ for "
        "negative ones)",
      loc);
    return number;
  };
  const exprt payload = payload_of(value);
  const exprt length = payload_length(payload, dest, loc);
  const exprt offset = operand(part.parts);
  const auto minimum = [](const exprt &a, const exprt &b) {
    return if_exprt{binary_relation_exprt{a, ID_le, b}, a, b};
  };
  const exprt start = minimum(offset, length);
  exprt end = length;
  if(!part.replacement.empty())
    end = minimum(plus_exprt{start, operand(part.replacement)}, length);
  return make_value(payload_substring(payload, start, end, dest, loc));
}
