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
  return index_exprt{positional_array(), from_integer(n - 1, c_index_type())};
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
      list.array, typecast_exprt::conditional_cast(list.count, c_index_type())},
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
      index_exprt{positional_array(), from_integer(i, c_index_type())},
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
    if(is_quoted_at(*it))
    {
      append_positional_parameters(list, dest, loc);
      dynamic = true;
    }
    else if(contains_at(*it))
    {
      // "x$@y" joins the first and last fields with the surrounding text;
      // unquoted $@ is subject to field splitting.
      unsupported(loc, "$@ other than as a whole word \"$@\"");
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

exprt shell_convertert::join_positional_parameters(
  code_blockt &dest,
  const source_locationt &loc)
{
  const symbol_exprt result = temporary(string_type(), dest, loc);
  assign(dest, result, string_literal("", loc), loc);
  for(std::size_t i = 0; i < max_arguments; ++i)
  {
    code_blockt append;
    exprt joined = result;
    if(i > 0)
      joined = concat(joined, string_literal(" ", loc), append, loc);
    joined = concat(
      joined,
      value_of(
        index_exprt{positional_array(), from_integer(i, c_index_type())}, loc),
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
  std::size_t n = 1;
  if(command.words.size() == 2)
  {
    const auto text = static_text(command.words[1]);
    if(
      !text.has_value() || text->empty() || text->size() > 9 ||
      text->find_first_not_of("0123456789") != std::string::npos)
    {
      unsupported(loc, "shift operand other than a decimal number");
    }
    n = std::stoul(*text);
  }

  // Shifting by more than $# is an error. dash, mksh, and posh exit, as
  // POSIX requires for special builtins (XCU 2.8.1); bash, zsh, busybox,
  // and yash fail with status 1.
  code_blockt error;
  if(
    program.dialect == shell_dialectt::POSIX ||
    program.dialect == shell_dialectt::MKSH)
  {
    exit_shell(error, true, loc);
  }
  else
    set_status(error, 1, loc);

  code_blockt shift;
  const exprt args = positional_array();
  for(std::size_t i = 0; i < max_arguments; ++i)
  {
    const index_exprt element{args, from_integer(i, c_index_type())};
    if(i + n < max_arguments)
    {
      assign(
        shift,
        element,
        index_exprt{args, from_integer(i + n, c_index_type())},
        loc);
    }
    else
      assign(shift, element, unset_string(loc), loc);
  }
  assign(
    shift,
    argument_count(),
    minus_exprt{argument_count(), from_integer(n, count_type())},
    loc);
  set_status(shift, 0, loc);

  add(
    dest,
    code_ifthenelset{
      binary_relation_exprt{
        argument_count(), ID_lt, from_integer(n, count_type())},
      std::move(error),
      std::move(shift)},
    loc);
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
      unsupported(
        loc,
        "set argument that is an expansion before the positional "
        "parameters (it could be an option)");
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
    code_assumet{binary_relation_exprt{
      argument_count(), ID_le, from_integer(max_arguments, count_type())}},
    loc);
  for(std::size_t i = 0; i < max_arguments; ++i)
  {
    const index_exprt element{
      positional_array(), from_integer(i, c_index_type())};
    code_blockt set, unset;
    assign(set, element, call(SHELL_RT_NONDET_STRING, {}, set, loc), loc);
    assign(unset, element, unset_string(loc), loc);
    add(
      init,
      code_ifthenelset{
        binary_relation_exprt{
          from_integer(i, count_type()), ID_lt, argument_count()},
        std::move(set),
        std::move(unset)},
      loc);
  }
}
