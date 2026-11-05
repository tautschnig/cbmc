/// \file
/// Shell converter: getopts (XCU getopts).
///
/// OPTIND is an ordinary variable, which every shell sets to 1 at
/// startup. Where getopts is within a group of options, as in -ab, the
/// shells keep a position of their own: the ghost shell::getopts_ind is
/// the index of the argument being parsed, shell::getopts_char the
/// position in it (0 at its start), and shell::getopts_optind the value
/// that getopts last gave OPTIND. Where OPTIND has another value, the
/// script assigned it, and parsing starts again at that argument.
///
/// Shells differ in OPTIND within a group (bash: the argument's index;
/// dash, busybox and mksh: the next one) and in OPTARG after an option
/// without argument (bash and mksh: unset; dash and busybox: empty). The
/// POSIX dialect allows either. zsh differs further and is unsupported.

#include "shell_convert_class.h"

#include <cctype>

symbol_exprt shell_convertert::getopts_ind()
{
  return global_symbol("shell::getopts_ind", "getopts_ind", integer_type());
}

symbol_exprt shell_convertert::getopts_char()
{
  return global_symbol("shell::getopts_char", "getopts_char", integer_type());
}

symbol_exprt shell_convertert::getopts_optind()
{
  return global_symbol(
    "shell::getopts_optind", "getopts_optind", string_type());
}

void shell_convertert::initialize_getopts(
  code_blockt &init,
  const source_locationt &loc)
{
  // Looking the ghosts up by name would intern their identifiers, which
  // reorders identifiers, and with them the variables of the refined
  // string solver: that alone made the csv-loop kernel take 40 s rather
  // than 3 s.
  if(!getopts_used)
    return;
  assign(init, getopts_ind(), from_integer(1, integer_type()), loc);
  assign(init, getopts_char(), from_integer(0, integer_type()), loc);
  assign(init, getopts_optind(), string_literal("1", loc), loc);
}

void shell_convertert::convert_getopts(
  const shell_commandt &command,
  code_blockt &dest)
{
  const source_locationt &loc = command.location;
  if(program.dialect == shell_dialectt::ZSH)
    unsupported(loc, "getopts in zsh");
  getopts_used = true;
  if(command.words.size() < 3)
    unsupported(loc, "getopts without an option string and a name");
  const auto optstring = static_text(command.words[1]);
  const auto name = static_text(command.words[2]);
  if(!optstring.has_value() || !name.has_value())
    unsupported(
      loc, "getopts with an option string or name that is not literal");
  for(char c : *optstring)
  {
    if(!std::isalnum(static_cast<unsigned char>(c)) && c != ':')
      unsupported(loc, "getopts option string with other than letters");
  }
  const bool silent = !optstring->empty() && (*optstring)[0] == ':';

  // The arguments, or the positional parameters.
  const field_listt arguments =
    command.words.size() > 3
      ? expand_fields(command.words.begin() + 3, command.words.end(), dest, loc)
      : [&]()
  {
    const field_listt list = new_field_list(dest, loc);
    append_positional_parameters(list, dest, loc);
    return list;
  }();
  const exprt count = typecast_exprt{arguments.count, integer_type()};
  const exprt one = from_integer(1, integer_type());
  const exprt zero = from_integer(0, integer_type());
  // The argument at a 1-based index, read at constant indices into a
  // temporary: the refined string solver does not support strings read at
  // symbolic indices.
  const auto argument = [&](const exprt &index, code_blockt &block)
  {
    const symbol_exprt value = temporary(string_type(), block, loc);
    assign(block, value, unset_string(loc), loc);
    for(std::size_t i = 0; i < max_arguments; ++i)
    {
      code_blockt read;
      assign(
        read,
        value,
        index_exprt{arguments.array, from_integer(i, index_type())},
        loc);
      add(
        block,
        code_ifthenelset{
          equal_exprt{index, from_integer(i + 1, integer_type())},
          std::move(read)},
        loc);
    }
    return value;
  };

  // Where the script assigned OPTIND, parsing starts at that argument.
  const symbol_exprt ind = temporary(integer_type(), dest, loc);
  const symbol_exprt position = temporary(integer_type(), dest, loc);
  assign(dest, ind, getopts_ind(), loc);
  assign(dest, position, getopts_char(), loc);
  {
    code_blockt assigned;
    const exprt optind = variable("OPTIND", loc);
    assign(
      assigned,
      ind,
      typecast_exprt::conditional_cast(
        arith_read("OPTIND", value_of(optind, loc), assigned, loc),
        integer_type()),
      loc);
    check(
      assigned,
      binary_relation_exprt{ind, ID_ge, one},
      "shell-unsupported",
      "OPTIND is positive where getopts runs (other values are not "
      "supported)",
      loc);
    assign(assigned, position, zero, loc);
    add(
      dest,
      code_ifthenelset{
        not_exprt{equal(value_of(optind, loc), getopts_optind(), dest, loc)},
        std::move(assigned)},
      loc);
  }

  const symbol_exprt result = temporary(string_type(), dest, loc);
  const symbol_exprt optarg = temporary(string_type(), dest, loc);
  const symbol_exprt done = temporary(bool_typet{}, dest, loc);
  assign(dest, optarg, unset_string(loc), loc);
  assign(dest, done, false_exprt{}, loc);

  // At the start of an argument: the end of the options, at an argument
  // that is not an option, after the last one, or after "--".
  {
    code_blockt start;
    const exprt arg = argument(ind, start);
    const exprt payload = payload_of(arg);
    const exprt length = payload_length(payload, start, loc);
    const exprt is_option = and_exprt{
      binary_relation_exprt{ind, ID_le, count},
      and_exprt{
        binary_relation_exprt{length, ID_ge, from_integer(2, integer_type())},
        equal_exprt{
          payload_byte(payload, zero, start, loc),
          from_integer('-', integer_type())}}};
    const symbol_exprt option = temporary(bool_typet{}, start, loc);
    assign(start, option, is_option, loc);
    code_blockt end_of_options;
    assign(end_of_options, done, true_exprt{}, loc);
    code_blockt double_dash;
    assign(double_dash, done, true_exprt{}, loc);
    assign(double_dash, ind, plus_exprt{ind, one}, loc);
    code_blockt first;
    assign(first, position, one, loc);
    code_blockt is_dash;
    add(
      is_dash,
      code_ifthenelset{
        equal(arg, string_literal("--", loc), is_dash, loc),
        std::move(double_dash),
        std::move(first)},
      loc);
    add(
      start,
      code_ifthenelset{option, std::move(is_dash), std::move(end_of_options)},
      loc);
    add(
      dest,
      code_ifthenelset{equal_exprt{position, zero}, std::move(start)},
      loc);
  }

  // The next option character.
  code_blockt parse;
  const exprt arg = argument(ind, parse);
  const exprt payload = payload_of(arg);
  const symbol_exprt length = temporary(integer_type(), parse, loc);
  assign(parse, length, payload_length(payload, parse, loc), loc);
  const symbol_exprt c = temporary(integer_type(), parse, loc);
  assign(parse, c, payload_byte(payload, position, parse, loc), loc);
  const exprt character = make_value(payload_substring(
    payload, position, plus_exprt{position, one}, parse, loc));
  assign(parse, position, plus_exprt{position, one}, loc);
  const exprt last = binary_relation_exprt{position, ID_ge, length};
  // The end of a group of options moves to the next argument.
  code_blockt next_argument;
  assign(next_argument, ind, plus_exprt{ind, one}, loc);
  assign(next_argument, position, zero, loc);
  const auto advance = [&](code_blockt &block)
  {
    code_blockt copy = next_argument;
    add(block, code_ifthenelset{last, std::move(copy)}, loc);
  };

  // An option that is not in the option string.
  code_blockt unknown;
  assign(unknown, result, string_literal("?", loc), loc);
  if(silent)
    assign(unknown, optarg, character, loc);
  advance(unknown);
  code_blockt chain = std::move(unknown);
  for(std::size_t i = optstring->size(); i-- > 0;)
  {
    const char letter = (*optstring)[i];
    if(letter == ':')
      continue;
    const bool takes_argument =
      i + 1 < optstring->size() && (*optstring)[i + 1] == ':';
    code_blockt matched;
    assign(matched, result, string_literal(std::string{letter}, loc), loc);
    if(takes_argument)
    {
      // The rest of the argument, or the next argument.
      code_blockt rest;
      assign(
        rest,
        optarg,
        make_value(payload_substring(payload, position, length, rest, loc)),
        loc);
      assign(rest, ind, plus_exprt{ind, one}, loc);
      assign(rest, position, zero, loc);
      code_blockt next;
      assign(next, optarg, argument(plus_exprt{ind, one}, next), loc);
      assign(next, ind, plus_exprt{ind, from_integer(2, integer_type())}, loc);
      assign(next, position, zero, loc);
      code_blockt missing;
      assign(missing, ind, plus_exprt{ind, one}, loc);
      assign(missing, position, zero, loc);
      if(silent)
      {
        assign(missing, result, string_literal(":", loc), loc);
        assign(missing, optarg, string_literal(std::string{letter}, loc), loc);
      }
      else
        assign(missing, result, string_literal("?", loc), loc);
      code_blockt at_end;
      add(
        at_end,
        code_ifthenelset{
          binary_relation_exprt{plus_exprt{ind, one}, ID_le, count},
          std::move(next),
          std::move(missing)},
        loc);
      add(
        matched,
        code_ifthenelset{not_exprt{last}, std::move(rest), std::move(at_end)},
        loc);
    }
    else
    {
      // bash and mksh unset OPTARG; dash and busybox make it empty.
      if(program.dialect == shell_dialectt::POSIX)
      {
        code_blockt empty;
        assign(empty, optarg, string_literal("", loc), loc);
        add(
          matched,
          code_ifthenelset{nondet(bool_typet{}, loc), std::move(empty)},
          loc);
      }
      advance(matched);
    }
    code_blockt otherwise = std::move(chain);
    chain = code_blockt{};
    add(
      chain,
      code_ifthenelset{
        equal_exprt{c, from_integer(letter, integer_type())},
        std::move(matched),
        std::move(otherwise)},
      loc);
  }
  parse.append(chain);
  add(dest, code_ifthenelset{not_exprt{done}, std::move(parse)}, loc);

  // The results: OPTIND, within a group of options the index of the
  // argument or of the next one.
  const symbol_exprt optind_value = temporary(integer_type(), dest, loc);
  exprt within = ind;
  const exprt next = plus_exprt{ind, one};
  if(program.dialect == shell_dialectt::MKSH)
    within = next;
  else if(program.dialect == shell_dialectt::POSIX)
    within = if_exprt{nondet(bool_typet{}, loc), exprt{ind}, next};
  assign(
    dest,
    optind_value,
    if_exprt{
      and_exprt{not_exprt{done}, notequal_exprt{position, zero}},
      within,
      exprt{ind}},
    loc);
  const exprt optind_string = from_integer_expr(optind_value, dest, loc);
  const symbol_exprt optind_text = temporary(string_type(), dest, loc);
  assign(dest, optind_text, optind_string, loc);
  assign_variable("OPTIND", optind_text, dest, loc);
  assign(dest, getopts_optind(), optind_text, loc);
  assign(dest, getopts_ind(), ind, loc);
  assign(dest, getopts_char(), position, loc);

  code_blockt end;
  assign(end, result, string_literal("?", loc), loc);
  add(dest, code_ifthenelset{done, std::move(end)}, loc);
  assign_variable(*name, result, dest, loc);
  // OPTARG may be unset, which assign_variable does not do.
  assign(dest, variable("OPTARG", loc), optarg, loc);
  known_literals.erase("OPTARG");
  code_blockt failed;
  set_status(failed, 1, loc);
  code_blockt found;
  set_status(found, 0, loc);
  add(dest, code_ifthenelset{done, std::move(failed), std::move(found)}, loc);
}
