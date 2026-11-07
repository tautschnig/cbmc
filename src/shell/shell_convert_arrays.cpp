/// \file
/// Shell converter: indexed arrays (bash, mksh).
///
/// A name that the script uses as an array anywhere (name=(...),
/// name[i]=value, ${name[i]}) is an array everywhere: the symbols
/// shell::array::NAME and shell::array::NAME#count hold its elements 0 to
/// count - 1, at most --shell-max-arguments of them. Arrays are dense:
/// assigning beyond the last element is reported. $name and name=value
/// are element 0. Associative arrays and negative subscripts are not
/// supported.

#include "shell_convert_class.h"

void shell_convertert::collect_arrays(const shell_command_listt &list)
{
  for(const auto &command : list)
  {
    if(
      !command.words.empty() &&
      static_text(command.words.front()) == std::string{"getopt"})
    {
      getopt_used = true;
    }
    for(const auto &assignment : command.assignments)
    {
      if(assignment.is_array || !assignment.index.empty())
        arrays.insert(assignment.name);
      collect_arrays(assignment.value);
      for(const auto &element : assignment.elements)
        collect_arrays(element);
    }
    for(const auto &word : command.words)
      collect_arrays(word);
    for(const auto &word : command.for_items)
      collect_arrays(word);
    collect_arrays(command.case_word);
    for(const auto &redirect : command.redirects)
    {
      collect_arrays(redirect.target);
      collect_arrays(redirect.here_document);
    }
    for(const auto *sub :
        {&command.body, &command.condition, &command.else_body})
    {
      collect_arrays(*sub);
    }
    for(const auto &item : command.case_items)
    {
      for(const auto &pattern : item.patterns)
        collect_arrays(pattern);
      collect_arrays(item.body);
    }
  }
}

void shell_convertert::collect_arrays(const shell_wordt &word)
{
  for(const auto &part : word)
  {
    if(part.kind == shell_word_partt::kindt::PARAMETER && !part.index.empty())
      arrays.insert(part.text);
    if(part.commands != nullptr)
      collect_arrays(*part.commands);
    collect_arrays(part.parts);
    collect_arrays(part.replacement);
    collect_arrays(part.index);
  }
}

shell_convertert::field_listt
shell_convertert::array_symbols(const std::string &name)
{
  check_parameter_name(name, source_locationt{});
  // zsh's arrays start at 1.
  if(program.dialect == shell_dialectt::ZSH)
    unsupported(source_locationt{}, "arrays in zsh");
  array_names_used.insert(name);
  return field_listt{
    global_symbol("shell::array::" + name, name, field_array_type()),
    global_symbol("shell::array::" + name + "#count", name, count_type())};
}

void shell_convertert::initialize_arrays(
  code_blockt &init,
  const source_locationt &loc)
{
  // From the environment, an array has one element, or none.
  for(const auto &name : array_names_used)
  {
    const field_listt array = array_symbols(name);
    // The ghost arrays of holes (see shell_convert_templates.cpp) and of
    // getopt (see shell_convert_utilities.cpp) are set before they are
    // used.
    if(name.rfind("__cprover_", 0) == 0)
    {
      assign(init, array.count, from_integer(0, count_type()), loc);
      continue;
    }
    const exprt value = environment_string(init, loc);
    assign(
      init,
      array.array,
      array_of_exprt{unset_string(loc), field_array_type()},
      loc);
    assign(
      init,
      index_exprt{array.array, from_integer(0, index_type())},
      value,
      loc);
    assign(
      init,
      array.count,
      if_exprt{
        is_set(value),
        from_integer(1, count_type()),
        from_integer(0, count_type())},
      loc);
  }
}

exprt shell_convertert::array_index(
  const shell_wordt &index,
  code_blockt &dest,
  const source_locationt &loc)
{
  // The subscript is an arithmetic expression, whose text must be known.
  const auto text = static_text(index);
  if(!text.has_value())
    unsupported(loc, "array subscript that is an expansion");
  const auto parsed = parse_shell_arithmetic(*text, arith_options);
  if(!parsed.expression.has_value())
    unsupported(loc, "array subscript that is not a valid expression");
  const symbol_exprt value = temporary(integer_type(), dest, loc);
  assign(
    dest,
    value,
    typecast_exprt::conditional_cast(
      arith(*parsed.expression, dest, loc), integer_type()),
    loc);
  check(
    dest,
    binary_relation_exprt{value, ID_ge, from_integer(0, integer_type())},
    "shell-unsupported",
    "the array subscript " + *text +
      " is not negative (negative subscripts are not supported)",
    loc);
  return value;
}

exprt shell_convertert::array_element(
  const field_listt &array,
  const exprt &index,
  code_blockt &dest,
  const source_locationt &loc)
{
  // Read at a constant index: the refined string solver does not support
  // strings read at symbolic indices.
  const symbol_exprt value = temporary(string_type(), dest, loc);
  assign(dest, value, unset_string(loc), loc);
  for(std::size_t k = 0; k < max_arguments; ++k)
  {
    code_blockt read;
    assign(
      read,
      value,
      index_exprt{array.array, from_integer(k, index_type())},
      loc);
    add(
      dest,
      code_ifthenelset{
        and_exprt{
          equal_exprt{index, from_integer(k, integer_type())},
          binary_relation_exprt{
            from_integer(k, count_type()), ID_lt, array.count}},
        std::move(read)},
      loc);
  }
  return value;
}

void shell_convertert::assign_array_element(
  const std::string &name,
  const exprt &index,
  const exprt &value,
  code_blockt &dest,
  const source_locationt &loc)
{
  const field_listt array = array_symbols(name);
  const exprt count =
    typecast_exprt::conditional_cast(array.count, integer_type());
  check(
    dest,
    and_exprt{
      binary_relation_exprt{index, ID_le, count},
      binary_relation_exprt{
        index, ID_lt, from_integer(max_arguments, integer_type())}},
    "shell-unsupported",
    "the array " + name +
      " stays dense (assigning beyond the element after the last one, or "
      "beyond --shell-max-arguments, is not supported)",
    loc);
  for(std::size_t k = 0; k < max_arguments; ++k)
  {
    code_blockt write;
    assign(
      write,
      index_exprt{array.array, from_integer(k, index_type())},
      value,
      loc);
    add(
      dest,
      code_ifthenelset{
        equal_exprt{index, from_integer(k, integer_type())}, std::move(write)},
      loc);
  }
  code_blockt grow;
  assign(
    grow,
    array.count,
    typecast_exprt::conditional_cast(
      plus_exprt{index, from_integer(1, integer_type())}, count_type()),
    loc);
  add(dest, code_ifthenelset{equal_exprt{index, count}, std::move(grow)}, loc);
}

void shell_convertert::convert_array_assignment(
  const shell_assignmentt &assignment,
  code_blockt &dest)
{
  const source_locationt &loc = assignment.location;
  const field_listt array = array_symbols(assignment.name);
  if(assignment.is_array)
  {
    // The elements are words subject to field splitting and pathname
    // expansion, as arguments are.
    const field_listt list = expand_fields(
      assignment.elements.begin(), assignment.elements.end(), dest, loc);
    check_field_list(list, dest, loc);
    if(!assignment.append)
    {
      assign(dest, array.array, list.array, loc);
      assign(dest, array.count, list.count, loc);
      return;
    }
    // name+=(...): after the elements, at constant indices.
    check(
      dest,
      binary_relation_exprt{
        plus_exprt{array.count, list.count},
        ID_le,
        from_integer(max_arguments, count_type())},
      "shell-argument-count",
      "array " + assignment.name + " exceeds --shell-max-arguments (" +
        std::to_string(max_arguments) + ")",
      loc);
    for(std::size_t j = 0; j < max_arguments; ++j)
    {
      code_blockt at;
      for(std::size_t i = 0; i + j < max_arguments; ++i)
      {
        code_blockt copy;
        assign(
          copy,
          index_exprt{array.array, from_integer(i + j, index_type())},
          index_exprt{list.array, from_integer(i, index_type())},
          loc);
        add(
          at,
          code_ifthenelset{
            binary_relation_exprt{
              from_integer(i, count_type()), ID_lt, list.count},
            std::move(copy)},
          loc);
      }
      add(
        dest,
        code_ifthenelset{
          equal_exprt{array.count, from_integer(j, count_type())},
          std::move(at)},
        loc);
    }
    assign(dest, array.count, plus_exprt{array.count, list.count}, loc);
    return;
  }
  const exprt index = assignment.index.empty()
                        ? exprt{from_integer(0, integer_type())}
                        : array_index(assignment.index, dest, loc);
  exprt value =
    expand_word(assignment.value, shell_word_contextt::ASSIGNMENT, dest, loc);
  if(assignment.append)
  {
    value = concat(
      value_of(array_element(array, index, dest, loc), loc), value, dest, loc);
  }
  assign_array_element(assignment.name, index, value, dest, loc);
}

bool shell_convertert::is_array_at(const shell_wordt &word, bool &quoted)
{
  using kindt = shell_word_partt::kindt;
  if(word.size() != 1)
    return false;
  const shell_word_partt *part = &word.front();
  quoted = part->kind == kindt::DOUBLE_QUOTED;
  if(quoted)
  {
    if(part->parts.size() != 1)
      return false;
    part = &part->parts.front();
  }
  if(
    part->kind != kindt::PARAMETER || part->index.size() != 1 ||
    part->parameter_length || !part->parameter_operator.empty())
  {
    return false;
  }
  const auto text = static_text(part->index);
  return text == std::string{"@"} || (!quoted && text == std::string{"*"});
}

void shell_convertert::append_array_elements(
  const field_listt &list,
  const shell_wordt &word,
  bool quoted,
  code_blockt &dest,
  const source_locationt &loc)
{
  const shell_word_partt &part =
    quoted ? word.front().parts.front() : word.front();
  const field_listt array = array_symbols(part.text);
  const std::string ifs = quoted || program.dialect == shell_dialectt::ZSH
                            ? std::string{}
                            : ifs_for_splitting(dest, loc);
  if(!quoted && !ifs_is_white(ifs))
    unsupported(
      loc, "unquoted array expansion with an IFS other than white space");
  if(quoted)
  {
    append_fields(list, array.array, array.count, dest, loc);
    return;
  }
  for(std::size_t i = 0; i < max_arguments; ++i)
  {
    code_blockt append;
    const exprt element =
      index_exprt{array.array, from_integer(i, index_type())};
    split_value(list, element, part, ifs, append, loc);
    add(
      dest,
      code_ifthenelset{
        binary_relation_exprt{
          from_integer(i, count_type()), ID_lt, array.count},
        std::move(append)},
      loc);
  }
}

exprt shell_convertert::expand_array_parameter(
  const shell_word_partt &part,
  shell_word_contextt context,
  bool quoted,
  code_blockt &dest)
{
  const source_locationt &loc = part.location;
  const field_listt array = array_symbols(part.text);
  const auto index_text = part.index.empty() ? std::optional<std::string>{"0"}
                                             : static_text(part.index);
  if(index_text == std::string{"@"} || index_text == std::string{"*"})
  {
    if(part.parameter_length && part.parameter_operator.empty())
      return from_integer_expr(array.count, dest, loc);
    if(!part.parameter_operator.empty() || part.parameter_length)
      unsupported(loc, "${name[@]} with an operator");
    // Elsewhere than as a whole word: the elements joined, as for "$*"
    // (with a space for @, which bash and mksh use in assignments).
    if(*index_text == "@" && context != shell_word_contextt::ASSIGNMENT)
      unsupported(
        loc, "${name[@]} where it does not expand to a list of fields");
    std::string separator = " ";
    if(*index_text == "*")
    {
      if(const auto ifs = known_ifs())
        separator = ifs->substr(0, 1);
      else
        ifs_for_splitting(dest, loc);
    }
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
        value_of(index_exprt{array.array, from_integer(i, index_type())}, loc),
        append,
        loc);
      assign(append, result, joined, loc);
      add(
        dest,
        code_ifthenelset{
          binary_relation_exprt{
            from_integer(i, count_type()), ID_lt, array.count},
          std::move(append)},
        loc);
    }
    return result;
  }
  const exprt index = part.index.empty()
                        ? exprt{from_integer(0, integer_type())}
                        : array_index(part.index, dest, loc);
  const exprt element = array_element(array, index, dest, loc);
  const std::string &op = part.parameter_operator;
  if(op.empty() || op[0] == '%' || op[0] == '#' || op[0] == '/')
    check_nounset(element, part.text, dest, loc);
  if(part.parameter_length)
  {
    if(!op.empty())
      unsupported(loc, "${#parameter} with an operator");
    return from_integer_expr(string_length(element, dest, loc), dest, loc);
  }
  if(op.empty())
    return value_of(element, loc);
  if(op.back() == '=')
    unsupported(loc, "assigning to an array element in an expansion");
  return expand_parameter_operator(part, element, false, context, quoted, dest);
}
