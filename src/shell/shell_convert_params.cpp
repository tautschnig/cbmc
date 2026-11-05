/// \file
/// Shell converter: positional parameters and lists of fields
/// (POSIX.1-2024 XCU 2.5.1, 2.5.2, and the set and shift builtins).
///
/// The positional parameters are an array of --shell-max-arguments
/// strings with a count. Operations on them are unrolled over the array,
/// so they need no loop unwinding.

#include "shell_convert_class.h"

exprt shell_convertert::positional_parameter(
  std::size_t n,
  const source_locationt &loc)
{
  // Elements at and after $# are unset, and there are never more than
  // max_arguments parameters.
  if(n > max_arguments)
    return unset_string(loc);
  return index_exprt{positional_array(), from_integer(n - 1, index_type())};
}

shell_convertert::field_listt
shell_convertert::new_field_list(code_blockt &dest, const source_locationt &loc)
{
  field_listt list{
    temporary(field_array_type(), dest, loc),
    temporary(count_type(), dest, loc)};
  assign(
    dest,
    list.array,
    array_of_exprt{unset_string(loc), field_array_type()},
    loc);
  assign(dest, list.count, from_integer(0, count_type()), loc);
  return list;
}

void shell_convertert::append_field(
  const field_listt &list,
  const exprt &value,
  code_blockt &dest,
  const source_locationt &loc)
{
  // Fields beyond the bound are counted but not stored; see
  // check_field_list.
  code_blockt store;
  assign(
    store,
    index_exprt{
      list.array, typecast_exprt::conditional_cast(list.count, index_type())},
    value,
    loc);
  add(
    dest,
    code_ifthenelset{
      binary_relation_exprt{
        list.count, ID_lt, from_integer(max_arguments, count_type())},
      std::move(store)},
    loc);
  assign(
    dest,
    list.count,
    plus_exprt{list.count, from_integer(1, count_type())},
    loc);
}

void shell_convertert::append_positional_parameters(
  const field_listt &list,
  code_blockt &dest,
  const source_locationt &loc)
{
  for(std::size_t i = 0; i < max_arguments; ++i)
  {
    code_blockt append;
    append_field(
      list,
      index_exprt{positional_array(), from_integer(i, index_type())},
      append,
      loc);
    add(
      dest,
      code_ifthenelset{
        binary_relation_exprt{
          from_integer(i, count_type()), ID_lt, argument_count()},
        std::move(append)},
      loc);
  }
}

/// Whether \p word is exactly "$@".
bool shell_convertert::is_quoted_at(const shell_wordt &word)
{
  using kindt = shell_word_partt::kindt;
  return word.size() == 1 && word[0].kind == kindt::DOUBLE_QUOTED &&
         word[0].parts.size() == 1 &&
         word[0].parts[0].kind == kindt::PARAMETER &&
         word[0].parts[0].text == "@" &&
         word[0].parts[0].parameter_operator.empty() &&
         !word[0].parts[0].parameter_length;
}

std::optional<std::pair<shell_wordt, shell_wordt>>
shell_convertert::split_at(const shell_wordt &word)
{
  using kindt = shell_word_partt::kindt;
  for(std::size_t i = 0; i < word.size(); ++i)
  {
    if(word[i].kind != kindt::DOUBLE_QUOTED)
      continue;
    const auto &inner = word[i].parts;
    for(std::size_t j = 0; j < inner.size(); ++j)
    {
      const shell_word_partt &part = inner[j];
      if(
        part.kind != kindt::PARAMETER || part.text != "@" ||
        !part.parameter_operator.empty() || part.parameter_length)
      {
        continue;
      }
      // The text before and after $@, each with its quotes.
      shell_wordt before{word.begin(), word.begin() + i};
      shell_wordt after{word.begin() + i + 1, word.end()};
      shell_word_partt quoted_before = word[i];
      quoted_before.parts.assign(inner.begin(), inner.begin() + j);
      shell_word_partt quoted_after = word[i];
      quoted_after.parts.assign(inner.begin() + j + 1, inner.end());
      before.push_back(quoted_before);
      after.insert(after.begin(), quoted_after);
      if(contains_at(before) || contains_at(after))
        return {};
      return {{before, after}};
    }
  }
  return {};
}

void shell_convertert::append_positional_parameters_between(
  const field_listt &list,
  const shell_wordt &before,
  const shell_wordt &after,
  code_blockt &dest,
  const source_locationt &loc)
{
  const exprt prefix =
    expand_word(before, shell_word_contextt::FIELD, dest, loc);
  const exprt suffix =
    expand_word(after, shell_word_contextt::FIELD, dest, loc);
  // Without positional parameters, the word is one field of the text
  // around $@.
  code_blockt none;
  append_field(list, concat(prefix, suffix, none, loc), none, loc);
  // Otherwise, the first parameter follows the text before, and the last
  // one is followed by the text after.
  code_blockt some;
  const exprt last =
    minus_exprt{argument_count(), from_integer(1, count_type())};
  for(std::size_t i = 0; i < max_arguments; ++i)
  {
    code_blockt append;
    const exprt index = from_integer(i, count_type());
    const symbol_exprt field = temporary(string_type(), append, loc);
    assign(
      append,
      field,
      index_exprt{positional_array(), from_integer(i, index_type())},
      loc);
    if(i == 0)
      assign(append, field, concat(prefix, field, append, loc), loc);
    code_blockt at_end;
    assign(at_end, field, concat(field, suffix, at_end, loc), loc);
    add(
      append,
      code_ifthenelset{equal_exprt{index, last}, std::move(at_end)},
      loc);
    append_field(list, field, append, loc);
    add(
      some,
      code_ifthenelset{
        binary_relation_exprt{index, ID_lt, argument_count()},
        std::move(append)},
      loc);
  }
  add(
    dest,
    code_ifthenelset{
      equal_exprt{argument_count(), from_integer(0, count_type())},
      std::move(none),
      std::move(some)},
    loc);
}

void shell_convertert::split_positional_parameters(
  const field_listt &list,
  const shell_word_partt &part,
  code_blockt &dest,
  const source_locationt &loc)
{
  // Each positional parameter is a field, which is then split as unquoted
  // expansions are (XCU 2.5.2, 2.6.5); zsh does not split them, but also
  // removes empty fields.
  const std::string ifs = program.dialect == shell_dialectt::ZSH
                            ? std::string{}
                            : ifs_for_splitting(dest, loc);
  if(!ifs_is_white(ifs))
  {
    // Shells differ in the empty fields between parameters.
    unsupported(loc, "unquoted $@ with an IFS other than white space");
  }
  for(std::size_t i = 0; i < max_arguments; ++i)
  {
    code_blockt split;
    split_value(
      list,
      index_exprt{positional_array(), from_integer(i, index_type())},
      part,
      ifs,
      split,
      loc);
    add(
      dest,
      code_ifthenelset{
        binary_relation_exprt{
          from_integer(i, count_type()), ID_lt, argument_count()},
        std::move(split)},
      loc);
  }
}

/// Whether \p word expands $@ anywhere.
bool shell_convertert::contains_at(const shell_wordt &word)
{
  using kindt = shell_word_partt::kindt;
  for(const auto &part : word)
  {
    if(part.kind == kindt::PARAMETER && part.text == "@")
      return true;
    if(
      (part.kind == kindt::DOUBLE_QUOTED || part.kind == kindt::PARAMETER) &&
      contains_at(part.parts))
    {
      return true;
    }
  }
  return false;
}

shell_convertert::field_listt shell_convertert::expand_fields(
  std::vector<shell_wordt>::const_iterator begin,
  std::vector<shell_wordt>::const_iterator end,
  code_blockt &dest,
  const source_locationt &loc)
{
  const field_listt list = new_field_list(dest, loc);
  bool dynamic = false;
  for(auto it = begin; it != end; ++it)
  {
    bool array_quoted = false;
    if(is_quoted_at(*it))
    {
      append_positional_parameters(list, dest, loc);
      dynamic = true;
    }
    else if(
      is_array_at(*it, array_quoted) &&
      arrays.count(
        array_quoted ? it->front().parts.front().text : it->front().text) != 0)
    {
      append_array_elements(list, *it, array_quoted, dest, loc);
      dynamic = true;
    }
    else if(const auto around = split_at(*it))
    {
      append_positional_parameters_between(
        list, around->first, around->second, dest, loc);
      dynamic = true;
    }
    else if(
      it->size() == 1 &&
      it->front().kind == shell_word_partt::kindt::PARAMETER &&
      it->front().text == "@" && it->front().parameter_operator.empty() &&
      !it->front().parameter_length)
    {
      split_positional_parameters(list, it->front(), dest, loc);
      dynamic = true;
    }
    else if(contains_at(*it))
      unsupported(loc, "$@ other than as a whole word \"$@\" or $@");
    else if(is_splittable(*it))
    {
      split_fields(list, it->front(), dest, loc);
      dynamic = true;
    }
    else if(const auto pattern = pathname_pattern(*it))
    {
      expand_pathnames(list, *it, *pattern, dest, loc);
      dynamic = true;
    }
    else
    {
      append_field(
        list,
        expand_word(*it, shell_word_contextt::FIELD, dest, loc),
        dest,
        loc);
    }
  }
  // A list of only single fields has a length known here.
  if(dynamic || static_cast<std::size_t>(end - begin) > max_arguments)
    check_field_list(list, dest, loc);
  return list;
}

bool shell_convertert::is_splittable(const shell_wordt &word)
{
  using kindt = shell_word_partt::kindt;
  if(word.size() != 1)
    return false;
  const shell_word_partt &part = word.front();
  if(part.kind == kindt::COMMAND_SUBSTITUTION)
    return true;
  // $? and $# are decimal numbers, which field splitting leaves as they
  // are; $@ and $* are fields of their own.
  return part.kind == kindt::PARAMETER && part.text != "?" &&
         part.text != "#" && part.text != "@" && part.text != "*";
}

void shell_convertert::check_field_list(
  const field_listt &list,
  code_blockt &dest,
  const source_locationt &loc)
{
  check(
    dest,
    binary_relation_exprt{
      list.count, ID_le, from_integer(max_arguments, count_type())},
    "shell-argument-count",
    "list of fields exceeds --shell-max-arguments (" +
      std::to_string(max_arguments) + ")",
    loc);
}

exprt shell_convertert::join_with_spaces(
  code_blockt &dest,
  const source_locationt &loc)
{
  return join_positional_parameters(" ", dest, loc);
}

exprt shell_convertert::join_positional_parameters(
  code_blockt &dest,
  const source_locationt &loc)
{
  // The fields are joined with the first byte of IFS, a space if it is
  // unset, and nothing if it is empty.
  std::string separator = " ";
  if(const auto ifs = known_ifs())
    separator = ifs->substr(0, 1);
  else
    ifs_for_splitting(dest, loc);
  return join_positional_parameters(separator, dest, loc);
}

exprt shell_convertert::join_positional_parameters(
  const std::string &separator,
  code_blockt &dest,
  const source_locationt &loc)
{
  const symbol_exprt result = temporary(string_type(), dest, loc);
  assign(dest, result, string_literal("", loc), loc);
  for(std::size_t i = 0; i < max_arguments; ++i)
  {
    code_blockt append;
    exprt joined = result;
    if(i > 0 && !separator.empty())
      joined = concat(joined, string_literal(separator, loc), append, loc);
    joined = concat(
      joined,
      value_of(
        index_exprt{positional_array(), from_integer(i, index_type())}, loc),
      append,
      loc);
    assign(append, result, joined, loc);
    add(
      dest,
      code_ifthenelset{
        binary_relation_exprt{
          from_integer(i, count_type()), ID_lt, argument_count()},
        std::move(append)},
      loc);
  }
  return result;
}

void shell_convertert::set_positional_parameters(
  const field_listt &list,
  code_blockt &dest,
  const source_locationt &loc)
{
  assign(dest, positional_array(), list.array, loc);
  assign(dest, argument_count(), list.count, loc);
}

/// shift [n] (XCU 2.15 shift): only a literal n is supported.
void shell_convertert::convert_shift(
  const shell_commandt &command,
  code_blockt &dest)
{
  const source_locationt &loc = command.location;
  if(command.words.size() > 2)
    unsupported(loc, "shift with several operands");
  // The operand: a literal number, or an expansion, as in
  // shift $((OPTIND - 1)), whose value is a decimal number.
  exprt n = from_integer(1, integer_type());
  if(command.words.size() == 2)
  {
    const auto text = static_text(command.words[1]);
    if(
      text.has_value() && !text->empty() && text->size() <= 9 &&
      text->find_first_not_of("0123456789") == std::string::npos)
    {
      n = from_integer(std::stoul(*text), integer_type());
    }
    else if(text.has_value())
      unsupported(loc, "shift operand other than a decimal number");
    else
    {
      const exprt value =
        expand_word(command.words[1], shell_word_contextt::FIELD, dest, loc);
      const symbol_exprt number = temporary(integer_type(), dest, loc);
      check(
        dest,
        parse_test_integer(value, trailing_blankst::NONE, number, dest, loc),
        "shell-unsupported",
        "the operand of shift is a decimal number (other values are not "
        "supported)",
        loc);
      check(
        dest,
        binary_relation_exprt{number, ID_ge, from_integer(0, integer_type())},
        "shell-unsupported",
        "the operand of shift is not negative (shells differ for negative "
        "ones)",
        loc);
      n = number;
    }
  }

  // Shifting by more than $# is an error. dash, mksh, and posh exit, as
  // POSIX requires for special builtins (XCU 2.8.1); bash (also as sh),
  // zsh, busybox, and yash fail with status 1.
  code_blockt error;
  code_blockt leave;
  exit_shell(leave, true, loc);
  if(program.dialect == shell_dialectt::MKSH)
    error = std::move(leave);
  else if(program.dialect == shell_dialectt::POSIX)
  {
    code_blockt fail;
    set_status(fail, 1, loc);
    add(
      error,
      code_ifthenelset{
        nondet(bool_typet{}, loc), std::move(leave), std::move(fail)},
      loc);
  }
  else
    set_status(error, 1, loc);

  code_blockt shift;
  const exprt args = positional_array();
  // In increasing order, each element reads one that is not yet moved,
  // at a constant index: the refined string solver does not support
  // strings read at symbolic indices.
  for(std::size_t i = 0; i < max_arguments; ++i)
  {
    const index_exprt element{args, from_integer(i, index_type())};
    if(n.is_constant())
    {
      const std::size_t k =
        numeric_cast_v<std::size_t>(to_constant_expr(n)) + i;
      assign(
        shift,
        element,
        k < max_arguments
          ? exprt{index_exprt{args, from_integer(k, index_type())}}
          : unset_string(loc),
        loc);
      continue;
    }
    code_blockt moved;
    assign(moved, element, unset_string(loc), loc);
    for(std::size_t k = i; k < max_arguments; ++k)
    {
      code_blockt read;
      assign(
        read, element, index_exprt{args, from_integer(k, index_type())}, loc);
      add(
        moved,
        code_ifthenelset{
          equal_exprt{n, from_integer(k - i, integer_type())}, std::move(read)},
        loc);
    }
    shift.append(moved);
  }
  const exprt shifted =
    n.is_constant()
      ? from_integer(
          numeric_cast_v<mp_integer>(to_constant_expr(n)), count_type())
      : typecast_exprt::conditional_cast(n, count_type());
  assign(shift, argument_count(), minus_exprt{argument_count(), shifted}, loc);
  set_status(shift, 0, loc);

  const exprt too_far =
    n.is_constant()
      ? binary_relation_exprt{argument_count(), ID_lt, shifted}
      : binary_relation_exprt{
          typecast_exprt::conditional_cast(argument_count(), integer_type()),
          ID_lt,
          n};
  add(dest, code_ifthenelset{too_far, std::move(error), std::move(shift)}, loc);
}

/// set (XCU 2.15 set): without arguments it lists variables, which is
/// only output; with "--" or a first argument that is not an option, the
/// remaining arguments replace the positional parameters.
void shell_convertert::convert_set(
  const shell_commandt &command,
  code_blockt &dest)
{
  const source_locationt &loc = command.location;
  auto first = command.words.begin() + 1;
  const auto end = command.words.end();
  bool replace = false;
  while(first != end)
  {
    const auto text = static_text(*first);
    if(!text.has_value())
    {
      // The fields replace the positional parameters, if there are any and
      // the first is not an option; without fields, set writes the
      // variables.
      const field_listt list = expand_fields(first, end, dest, loc);
      code_blockt replace_them;
      const symbol_exprt field = temporary(string_type(), replace_them, loc);
      assign(
        replace_them,
        field,
        index_exprt{list.array, from_integer(0, index_type())},
        loc);
      check(
        replace_them,
        not_exprt{or_exprt{
          payload_has_prefix(
            payload_of(field), payload_literal("-"), replace_them, loc),
          payload_has_prefix(
            payload_of(field), payload_literal("+"), replace_them, loc)}},
        "shell-unsupported",
        "the first field of " + display(*first) +
          " does not start with - or + (it would be an option)",
        loc);
      set_positional_parameters(list, replace_them, loc);
      set_status(replace_them, 0, loc);
      code_blockt list_variables;
      nondet_status(list_variables, 255, loc);
      add(
        dest,
        code_ifthenelset{
          binary_relation_exprt{
            list.count, ID_gt, from_integer(0, count_type())},
          std::move(replace_them),
          std::move(list_variables)},
        loc);
      return;
    }
    if(*text == "--")
    {
      ++first;
      replace = true;
      break;
    }
    if(text->empty() || ((*text)[0] != '-' && (*text)[0] != '+'))
    {
      replace = true;
      break;
    }
    first = convert_set_option(first, end, dest, loc);
    if(invalid_set_option)
    {
      invalid_set_option = false;
      set_status(dest, 2, loc);
      return;
    }
  }
  if(replace)
    set_positional_parameters(expand_fields(first, end, dest, loc), dest, loc);
  set_status(dest, 0, loc);
}

void shell_convertert::initialize_positional_parameters(
  code_blockt &init,
  const source_locationt &loc)
{
  // $# is nondeterministic, within the bound.
  assign(init, argument_count(), nondet(count_type(), loc), loc);
  add(
    init,
    code_assumet{and_exprt{
      binary_relation_exprt{
        argument_count(), ID_ge, from_integer(0, count_type())},
      binary_relation_exprt{
        argument_count(), ID_le, from_integer(max_arguments, count_type())}}},
    loc);
  for(std::size_t i = 0; i < max_arguments; ++i)
  {
    const index_exprt element{
      positional_array(), from_integer(i, index_type())};
    const exprt present = binary_relation_exprt{
      from_integer(i, count_type()), ID_lt, argument_count()};
    // Without a branch, which would make the string a choice between two
    // strings: the string solvers then index into every alternative.
    // Unset values are empty (see environment_string).
    const exprt value = nondet_string(init, loc);
    add(init, code_assumet{or_exprt{present, is_empty(value)}}, loc);
    exprt set_value = value;
    to_struct_expr(set_value).operands()[0] =
      typecast_exprt{present, c_bool_type()};
    assign(init, element, set_value, loc);
  }
}
