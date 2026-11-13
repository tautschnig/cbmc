/*******************************************************************\

Module: Built-in function for String.format

Author: Romain Brenguier, Joel Allred

\*******************************************************************/

/// \file
///  Built-in function for String.format

#include "string_format_builtin_function.h"

#include <util/bitvector_expr.h>
#include <util/ieee_float.h>
#include <util/message.h>
#include <util/range.h>
#include <util/simplify_expr.h>
#include <util/symbol_table.h>

#include "format_specifier.h"
#include "string_float_format.h"
#include "string_refinement_util.h"

#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>

static exprt format_arg_from_string(
  const array_string_exprt &string,
  const irep_idt &id,
  array_poolt &array_pool);

string_format_builtin_functiont::string_format_builtin_functiont(
  const exprt &return_code,
  const std::vector<exprt> &fun_args,
  array_poolt &array_pool)
  : string_builtin_functiont(return_code, array_pool)
{
  PRECONDITION(fun_args.size() >= 3);
  result = array_pool.find(fun_args[1], fun_args[0]);
  const array_string_exprt format_string_expr =
    get_string_expr(array_pool, fun_args[2]);

  // List of arguments after the format string
  inputs = make_range(fun_args.begin() + 3, fun_args.end())
             .map([&](const exprt &arg) {
               INVARIANT(
                 is_refined_string_type(arg.type()),
                 "arguments of format should be strings");
               return get_string_expr(array_pool, arg);
             })
             .collect<std::vector<array_string_exprt>>();

  // format_string is only initialized if the expression is constant
  if(
    array_pool.get_or_create_length(format_string_expr).is_constant() &&
    format_string_expr.content().id() == ID_array)
  {
    const auto length = numeric_cast_v<std::size_t>(
      to_constant_expr(array_pool.get_or_create_length(format_string_expr)));
    format_string = utf16_constant_array_to_java(
      to_array_expr(format_string_expr.content()), length);
  }
}

#if 0
// This code is deactivated as it is not used for now, but ultimalety this
// should be used to throw an exception when the format string is not correct
/// Used to check first argument of `String.format` is correct.
/// \param s: a string
/// \return True if the argument is a correct format string. Any '%' found
///   between format specifier means the string is invalid.
static bool check_format_string(std::string s)
{
  std::string format_specifier=
      "%(\\d+\\$)?([-#+ 0,(\\<]*)?(\\d+)?(\\.\\d+)?([tT])?([a-zA-Z%])";
  std::regex regex(format_specifier);
  std::smatch match;

  while(std::regex_search(s, match, regex))
  {
    if(match.position()!= 0)
      for(const auto &c : match.str())
        if(c=='%')
          return false;
    s=match.suffix();
  }

  for(const auto &c : s)
    if(c=='%')
      return false;

  return true;
}
#endif

// The encoding of an argument by cproverFormatArgument of the Java models:
// a character for its kind, four characters for its value as a long (16 bits
// each, the most significant first: the value of an integral, char or
// boolean, the raw bits of the double value of a Float or Double), two for
// its hash code where a %h uses it, one for its properties (see
// formattable_property), and the text of String.valueOf of it where a %s
// uses it. A missing argument is "".
static const char kind_null = 'N';
static const char kind_string = 'S';
static const char kind_byte = 'Y';
static const char kind_short = 'H';
static const char kind_int = 'I';
static const char kind_long = 'L';
static const char kind_char = 'C';
static const char kind_boolean = 'B';
static const char kind_float = 'F';
static const char kind_double = 'D';
static const char kind_big_integer = 'G';
static const char kind_big_decimal = 'R';
static const char kind_calendar = 'T'; // Date or Calendar
static const char kind_temporal = 'U'; // another TemporalAccessor
static const char kind_other = 'O';
static const std::size_t hash_offset = 5;
static const std::size_t properties_offset = 7;
static const std::size_t text_offset = 8;
/// The property of an argument that implements java.util.Formattable.
static const unsigned formattable_property = 1;
/// The property of an argument whose hash code the encoding does not give.
static const unsigned unknown_hash_property = 2;

/// Whether the argument \p arg is of kind \p kind.
static exprt kind_is(const array_string_exprt &arg, char kind)
{
  return equal_exprt{
    arg[from_integer(0, arg.length_type())],
    from_integer(kind, to_type_with_subtype(arg.content().type()).subtype())};
}

/// Whether the argument \p arg is of one of \p kinds.
static exprt
kind_is_one_of(const array_string_exprt &arg, const std::string &kinds)
{
  exprt::operandst disjuncts;
  for(const char k : kinds)
    disjuncts.push_back(kind_is(arg, k));
  return disjunction(disjuncts);
}

/// The kinds of Byte, Short, Integer and Long.
static const std::string integral_kinds =
  std::string{kind_byte} + kind_short + kind_int + kind_long;
/// The kinds of the arguments that may be a Formattable.
static const std::string object_kinds = std::string{kind_big_integer} +
                                        kind_big_decimal + kind_calendar +
                                        kind_temporal + kind_other;

/// Expression which is true when the argument is null
static exprt is_null(const array_string_exprt &string, array_poolt &)
{
  return kind_is(string, kind_null);
}

/// The type of the values of unsigned_value and hash_value: wide enough for
/// an unsigned long, signed for the string functions.
static const signedbv_typet unsigned_value_type{65};

/// The value that %o and %x print of a Byte, Short, Integer or Long \p arg,
/// as java.util.Formatter: a negative value plus 2^8, 2^16, 2^32 or 2^64.
static exprt unsigned_value(const array_string_exprt &arg, array_poolt &pool)
{
  const exprt value = format_arg_from_string(arg, ID_int, pool);
  const auto bits = [&](std::size_t width) -> exprt
  {
    return typecast_exprt{
      typecast_exprt{value, unsignedbv_typet{width}}, unsigned_value_type};
  };
  return if_exprt{
    kind_is(arg, kind_byte),
    bits(8),
    if_exprt{
      kind_is(arg, kind_short),
      bits(16),
      if_exprt{kind_is(arg, kind_int), bits(32), bits(64)}}};
}

/// The double value of a Float or Double \p arg, from its bits.
static exprt double_value(const array_string_exprt &arg, array_poolt &pool)
{
  return typecast_exprt{
    typecast_exprt{format_arg_from_string(arg, ID_int, pool), bv_typet{64}},
    ieee_float_spect::double_precision().to_type()};
}

/// The hash code of \p arg, as an unsigned int, which %h prints.
static exprt hash_value(const array_string_exprt &arg)
{
  const typet &index_type = arg.length_type();
  const unsignedbv_typet type{32};
  return typecast_exprt{
    bitor_exprt{
      shl_exprt{
        typecast_exprt{arg[from_integer(hash_offset, index_type)], type}, 16},
      typecast_exprt{arg[from_integer(hash_offset + 1, index_type)], type}},
    unsigned_value_type};
}

/// Whether the encoding gives the hash code of \p arg.
static exprt hash_is_known(const array_string_exprt &arg)
{
  const typet &char_type = to_type_with_subtype(arg.content().type()).subtype();
  return and_exprt{
    not_exprt{kind_is(arg, kind_null)},
    equal_exprt{
      bitand_exprt{
        arg[from_integer(properties_offset, arg.length_type())],
        from_integer(unknown_hash_property, char_type)},
      from_integer(0, char_type)}};
}

/// The value that %tQ (\p field 'Q') or %ts (\p field 's') prints of a Long
/// \p arg: its milliseconds, or its milliseconds / 1000 (rounded towards
/// zero, as Formatter divides).
static exprt
epoch_value(const array_string_exprt &arg, char field, array_poolt &pool)
{
  const exprt value = format_arg_from_string(arg, ID_int, pool);
  if(field == 'Q')
    return value;
  return div_exprt{value, from_integer(1000, value.type())};
}

/// Axioms that \p res is \p text where \p condition holds.
static exprt string_is_constant_if(
  const exprt &condition,
  const array_string_exprt &res,
  const std::string &text,
  array_poolt &array_pool)
{
  const typet &index_type = res.length_type();
  const typet &char_type = to_type_with_subtype(res.content().type()).subtype();
  exprt::operandst conjuncts{equal_exprt{
    array_pool.get_or_create_length(res),
    from_integer(text.size(), index_type)}};
  for(std::size_t i = 0; i < text.size(); ++i)
  {
    conjuncts.push_back(equal_exprt{
      res[from_integer(i, index_type)],
      from_integer(static_cast<unsigned char>(text[i]), char_type)});
  }
  return implies_exprt{condition, conjunction(conjuncts)};
}

/// Axioms that \p res is \p source where \p condition holds.
static void string_equals_if(
  string_constraint_generatort &generator,
  const exprt &condition,
  const array_string_exprt &res,
  const array_string_exprt &source,
  string_constraintst &constraints,
  message_handlert &message_handler)
{
  array_poolt &array_pool = generator.array_pool;
  const exprt source_length = array_pool.get_or_create_length(source);
  constraints.existential.push_back(implies_exprt{
    condition,
    equal_exprt{array_pool.get_or_create_length(res), source_length}});
  const symbol_exprt qvar =
    generator.fresh_symbol("QA_format_copy", res.length_type());
  constraints.universal.emplace_back(
    qvar,
    zero_if_negative(source_length),
    implies_exprt{condition, equal_exprt{res[qvar], source[qvar]}},
    generator.ns,
    message_handler);
}

/// The text of the argument \p arg, String.valueOf of it.
static std::pair<array_string_exprt, string_constraintst> argument_text(
  string_constraint_generatort &generator,
  const array_string_exprt &arg)
{
  array_poolt &array_pool = generator.array_pool;
  const typet &index_type = arg.length_type();
  const array_string_exprt text = array_pool.fresh_string(
    index_type, to_type_with_subtype(arg.content().type()).subtype());
  auto result = generator.add_axioms_for_substring(
    text,
    arg,
    from_integer(text_offset, index_type),
    array_pool.get_or_create_length(arg));
  return {text, std::move(result.second)};
}

/// Parse `s` and add axioms ensuring the output corresponds to the output of
/// String.format. Assumes the argument is a string representing one of:
/// string expr, int, float, char, boolean, hashcode, date_time.
/// The correct type will be retrieved by calling get_arg with an id depending
/// on the format specifier.
/// \param generator: constraint generator
/// \param fs: a format specifier
/// \param string_arg: format string from argument
/// \param index_type: type for indexes in strings
/// \param char_type: type of characters
/// \param message_handler: message handler for warnings
/// \return String expression representing the output of String.format.
static std::pair<array_string_exprt, string_constraintst>
add_axioms_for_format_specifier(
  string_constraint_generatort &generator,
  const format_specifiert &fs,
  const array_string_exprt &string_arg,
  const typet &index_type,
  const typet &char_type,
  message_handlert &message_handler)
{
  string_constraintst constraints;
  array_poolt &array_pool = generator.array_pool;
  const array_string_exprt res = array_pool.fresh_string(index_type, char_type);
  std::pair<exprt, string_constraintst> return_code;
  switch(fs.conversion)
  {
  case format_specifiert::DECIMAL_INTEGER:
  {
    const exprt null = is_null(string_arg, array_pool);
    const array_string_exprt digits =
      array_pool.fresh_string(index_type, char_type);
    return_code = generator.add_axioms_for_string_of_int(
      digits, format_arg_from_string(string_arg, ID_int, array_pool), 0);
    constraints = std::move(return_code.second);
    constraints.existential.push_back(
      string_is_constant_if(null, res, "null", array_pool));
    // (the digits of a BigInteger are not modelled)
    string_equals_if(
      generator,
      kind_is_one_of(string_arg, integral_kinds),
      res,
      digits,
      constraints,
      message_handler);
    return {res, std::move(constraints)};
  }
  case format_specifiert::HEXADECIMAL_INTEGER:
  case format_specifiert::OCTAL_INTEGER:
  {
    // the unsigned value of a Byte, Short, Integer or Long (the digits of a
    // BigInteger are not modelled)
    const exprt null = is_null(string_arg, array_pool);
    const array_string_exprt digits =
      array_pool.fresh_string(index_type, char_type);
    const unsigned radix =
      fs.conversion == format_specifiert::OCTAL_INTEGER ? 8 : 16;
    return_code = generator.add_axioms_for_string_of_int_with_radix(
      digits,
      unsigned_value(string_arg, array_pool),
      from_integer(radix, unsigned_value_type),
      0);
    constraints = std::move(return_code.second);
    constraints.existential.push_back(
      string_is_constant_if(null, res, "null", array_pool));
    string_equals_if(
      generator,
      kind_is_one_of(string_arg, integral_kinds),
      res,
      digits,
      constraints,
      message_handler);
    return {res, std::move(constraints)};
  }
  case format_specifiert::CHARACTER:
  {
    // null gives "null", a Character itself, and a Byte, Short or Integer
    // the character of its code point: one char in the basic plane, a
    // surrogate pair beyond it. (An invalid code point and other kinds
    // throw, see add_axioms_for_format_error.)
    const exprt null = is_null(string_arg, array_pool);
    constraints.existential.push_back(
      string_is_constant_if(null, res, "null", array_pool));
    const exprt value = format_arg_from_string(string_arg, ID_int, array_pool);
    const exprt basic = and_exprt{
      binary_relation_exprt{value, ID_ge, from_integer(0, value.type())},
      binary_relation_exprt{value, ID_le, from_integer(0xFFFF, value.type())}};
    const exprt supplementary = and_exprt{
      binary_relation_exprt{value, ID_gt, from_integer(0xFFFF, value.type())},
      binary_relation_exprt{
        value, ID_le, from_integer(0x10FFFF, value.type())}};
    const exprt code_point =
      kind_is_one_of(string_arg, std::string{kind_byte, kind_short, kind_int});
    const exprt single =
      or_exprt{kind_is(string_arg, kind_char), and_exprt{code_point, basic}};
    constraints.existential.push_back(implies_exprt{
      single,
      and_exprt{
        equal_exprt{
          array_pool.get_or_create_length(res), from_integer(1, index_type)},
        equal_exprt{
          res[from_integer(0, index_type)],
          typecast_exprt{
            string_arg[from_integer(4, index_type)], char_type}}}});
    const minus_exprt offset{value, from_integer(0x10000, value.type())};
    constraints.existential.push_back(implies_exprt{
      and_exprt{code_point, supplementary},
      conjunction(
        {equal_exprt{
           array_pool.get_or_create_length(res), from_integer(2, index_type)},
         equal_exprt{
           res[from_integer(0, index_type)],
           typecast_exprt{
             plus_exprt{
               from_integer(0xD800, value.type()),
               ashr_exprt{offset, from_integer(10, value.type())}},
             char_type}},
         equal_exprt{
           res[from_integer(1, index_type)],
           typecast_exprt{
             plus_exprt{
               from_integer(0xDC00, value.type()),
               bitand_exprt{offset, from_integer(0x3FF, value.type())}},
             char_type}}})});
    return {res, constraints};
  }
  case format_specifiert::BOOLEAN:
    return_code = generator.add_axioms_from_bool(
      res, format_arg_from_string(string_arg, ID_boolean, array_pool));
    return {res, std::move(return_code.second)};
  case format_specifiert::STRING:
    return argument_text(generator, string_arg);
  case format_specifiert::HASHCODE:
  {
    // Integer.toHexString of the hash code, or "null" (the hash code of a
    // String is not modelled)
    const array_string_exprt digits =
      array_pool.fresh_string(index_type, char_type);
    return_code = generator.add_axioms_for_string_of_int_with_radix(
      digits, hash_value(string_arg), from_integer(16, unsigned_value_type), 0);
    constraints = std::move(return_code.second);
    constraints.existential.push_back(string_is_constant_if(
      is_null(string_arg, array_pool), res, "null", array_pool));
    string_equals_if(
      generator,
      hash_is_known(string_arg),
      res,
      digits,
      constraints,
      message_handler);
    return {res, std::move(constraints)};
  }
  case format_specifiert::LINE_SEPARATOR:
    // TODO: the constant should depend on the system: System.lineSeparator()
    return_code = generator.add_axioms_for_constant(res, "\n");
    return {res, std::move(return_code.second)};
  case format_specifiert::PERCENT_SIGN:
    return_code = generator.add_axioms_for_constant(res, "%");
    return {res, std::move(return_code.second)};
  case format_specifiert::HEXADECIMAL_INTEGER_UPPER:
  case format_specifiert::SCIENTIFIC_UPPER:
  case format_specifiert::GENERAL_UPPER:
  case format_specifiert::HEXADECIMAL_FLOAT_UPPER:
  case format_specifiert::CHARACTER_UPPER:
  case format_specifiert::DATE_TIME_UPPER:
  case format_specifiert::BOOLEAN_UPPER:
  case format_specifiert::STRING_UPPER:
  case format_specifiert::HASHCODE_UPPER:
  {
    format_specifiert fs_lower = fs;
    fs_lower.conversion = tolower(fs.conversion);
    auto format_specifier_result = add_axioms_for_format_specifier(
      generator, fs_lower, string_arg, index_type, char_type, message_handler);

    const exprt return_code_upper_case =
      generator.fresh_symbol("return_code_upper_case", get_return_code_type());
    const string_to_upper_case_builtin_functiont upper_case_function(
      return_code_upper_case, res, format_specifier_result.first, array_pool);
    auto upper_case_constraints =
      upper_case_function.constraints(generator, message_handler);
    merge(upper_case_constraints, std::move(format_specifier_result.second));
    return {res, std::move(upper_case_constraints)};
  }
  case format_specifiert::DATE_TIME:
  {
    // the milliseconds (%tQ) or seconds (%ts) since the epoch of a Long, or
    // "null"; the other fields, and other arguments, depend on the time
    // zone and the locale, which are not modelled
    const exprt null = is_null(string_arg, array_pool);
    constraints.existential.push_back(
      string_is_constant_if(null, res, "null", array_pool));
    if(fs.date_time_field != 'Q' && fs.date_time_field != 's')
    {
      generator.approximation_warning(
        "String.format",
        std::string{"is not modelled for the format specifier %t"} +
          fs.date_time_field);
      return {res, constraints};
    }
    const array_string_exprt digits =
      array_pool.fresh_string(index_type, char_type);
    return_code = generator.add_axioms_for_string_of_int(
      digits, epoch_value(string_arg, fs.date_time_field, array_pool), 0);
    merge(constraints, std::move(return_code.second));
    string_equals_if(
      generator,
      kind_is(string_arg, kind_long),
      res,
      digits,
      constraints,
      message_handler);
    return {res, std::move(constraints)};
  }
  case format_specifiert::SCIENTIFIC:
  case format_specifiert::DECIMAL_FLOAT:
  case format_specifiert::GENERAL:
  case format_specifiert::HEXADECIMAL_FLOAT:
  {
    // Formatter's string of the double value of a Float or Double (see
    // java_format_float), or "null" (that of a BigDecimal is not modelled)
    const exprt null = is_null(string_arg, array_pool);
    const array_string_exprt printed =
      array_pool.fresh_string(index_type, char_type);
    constraints = generator.add_axioms_for_java_format_float(
      printed,
      double_value(string_arg, array_pool),
      fs.conversion,
      fs.precision);
    constraints.existential.push_back(
      string_is_constant_if(null, res, "null", array_pool));
    string_equals_if(
      generator,
      kind_is_one_of(string_arg, std::string{kind_float, kind_double}),
      res,
      printed,
      constraints,
      message_handler);
    return {res, std::move(constraints)};
  }
  }

  UNREACHABLE_BECAUSE(
    "format specifier must belong to [bBhHsScCdoxXeEfgGaAtT%n]");
}

/// Deserialize an argument for format from \p string (see kind_null).
/// \p id should be one of: string_expr, int, boolean.
/// The characters of `string` must be of type `unsignedbv_typet(16)`.
static exprt format_arg_from_string(
  const array_string_exprt &string,
  const irep_idt &id,
  array_poolt &array_pool)
{
  PRECONDITION(
    to_array_type(string.content().type()).element_type() ==
    unsignedbv_typet(16));

  if(id == "string_expr")
    return string;
  if(id == ID_int)
  {
    // the value, from the characters after the kind
    // (int64)string[1] << 48 | (int64)string[2] << 32 |
    // (int64)string[3] << 16 | (int64)string[4]
    const signedbv_typet type{64};
    const typet &index_type = string.length_type();
    const auto at = [&](std::size_t i) {
      return typecast_exprt{string[from_integer(i, index_type)], type};
    };
    return bitor_exprt{
      bitor_exprt{shl_exprt{at(1), 48}, shl_exprt{at(2), 32}},
      bitor_exprt{shl_exprt{at(3), 16}, at(4)}};
  }

  if(id == ID_boolean)
  {
    // Java's %b: false for null and Boolean.FALSE, true for anything else.
    return if_exprt{
      is_null(string, array_pool),
      false_exprt{},
      if_exprt{
        kind_is(string, kind_boolean),
        typecast_exprt{
          string[from_integer(4, string.length_type())], bool_typet()},
        true_exprt{}}};
  }
  UNHANDLED_CASE;
}

/// Whether \p fs takes an argument.
static bool takes_argument(const format_specifiert &fs)
{
  return fs.conversion != format_specifiert::PERCENT_SIGN &&
         fs.conversion != format_specifiert::LINE_SEPARATOR;
}

/// The position in the arguments of the argument of \p fs, if it takes one
/// and the specifier is modelled with \p argument_count arguments.
static std::optional<std::size_t>
argument_position(const format_specifiert &fs, std::size_t argument_count)
{
  if(
    !fs.is_modelled() || fs.index < 1 ||
    static_cast<std::size_t>(fs.index) > argument_count)
  {
    return {};
  }
  // The first argument has index 1.
  return static_cast<std::size_t>(fs.index - 1);
}

/// Parse `s` and add axioms ensuring the output corresponds to the output of
/// String.format.
/// \param generator: constraint generator
/// \param res: string expression for the result of the format function
/// \param s: a format string
/// \param args: a vector of arguments
/// \param message_handler: message handler for warnings
/// \return code, 0 on success
static std::pair<exprt, string_constraintst> add_axioms_for_format(
  string_constraint_generatort &generator,
  const array_string_exprt &res,
  const std::string &s,
  const std::vector<array_string_exprt> &args,
  message_handlert &message_handler)
{
  string_constraintst constraints;
  array_poolt &array_pool = generator.array_pool;
  const std::vector<format_elementt> format_strings = parse_format_string(s);
  std::vector<array_string_exprt> intermediary_strings;
  const typet &char_type = to_type_with_subtype(res.content().type()).subtype();
  const typet &index_type = res.length_type();

  array_string_exprt string_arg;

  for(const format_elementt &fe : format_strings)
  {
    if(fe.is_format_specifier())
    {
      const format_specifiert &fs = fe.get_format_specifier();
      const auto position = argument_position(fs, args.size());
      if(!fs.is_modelled() || (takes_argument(fs) && !position.has_value()))
      {
        generator.approximation_warning(
          "String.format",
          "is not modelled for flags, widths, precisions or missing "
          "arguments");
        intermediary_strings.push_back(
          array_pool.fresh_string(index_type, char_type));
        continue;
      }
      if(takes_argument(fs))
        string_arg = args[*position];

      auto result = add_axioms_for_format_specifier(
        generator, fs, string_arg, index_type, char_type, message_handler);
      merge(constraints, std::move(result.second));
      intermediary_strings.push_back(result.first);
    }
    else
    {
      const array_string_exprt str =
        array_pool.fresh_string(index_type, char_type);
      auto result = generator.add_axioms_for_constant(
        str, fe.get_format_text().get_content());
      merge(constraints, result.second);
      intermediary_strings.push_back(str);
    }
  }

  exprt return_code = from_integer(0, get_return_code_type());

  if(intermediary_strings.empty())
  {
    constraints.existential.push_back(equal_exprt(
      array_pool.get_or_create_length(res), from_integer(0, index_type)));
    return {return_code, constraints};
  }

  array_string_exprt str = intermediary_strings[0];

  if(intermediary_strings.size() == 1)
  {
    // Copy the first string
    auto result = generator.add_axioms_for_substring(
      res,
      str,
      from_integer(0, index_type),
      generator.array_pool.get_or_create_length(str));
    merge(constraints, std::move(result.second));
    return {result.first, std::move(constraints)};
  }

  // start after the first string and stop before the last
  for(std::size_t i = 1; i < intermediary_strings.size() - 1; ++i)
  {
    const array_string_exprt &intermediary = intermediary_strings[i];
    const array_string_exprt fresh =
      array_pool.fresh_string(index_type, char_type);
    auto result = generator.add_axioms_for_concat(fresh, str, intermediary);
    return_code = maximum(return_code, result.first);
    merge(constraints, std::move(result.second));
    str = fresh;
  }

  auto result =
    generator.add_axioms_for_concat(res, str, intermediary_strings.back());
  merge(constraints, std::move(result.second));
  return {maximum(result.first, return_code), std::move(constraints)};
}

/// The value of an encoded argument (see kind_null) as a long.
static mp_integer eval_long_arg(const std::vector<mp_integer> &encoded)
{
  PRECONDITION(encoded.size() >= text_offset);
  for(std::size_t i = 1; i < hash_offset; i++)
  {
    DATA_INVARIANT(
      encoded[i] <= 0xFFFF,
      "Component of serialized value to format must be bounded by 0xFFFF");
  }
  const int64_t int64_value =
    (encoded[1] << 48).to_long() | (encoded[2] << 32).to_long() |
    (encoded[3] << 16).to_long() | encoded[4].to_long();
  return mp_integer{int64_value};
}

static std::vector<mp_integer> deserialize_constant_int_arg(
  const std::vector<mp_integer> encoded,
  const unsigned base)
{
  const std::string long_as_string =
    integer2string(eval_long_arg(encoded), base);

  return make_range(long_as_string).map([&](char c) { return mp_integer{c}; });
}

static bool eval_is_null(const std::vector<mp_integer> &string)
{
  return !string.empty() && string[0] == kind_null;
}

/// The characters of \p text, with upper-case ASCII letters in lower case
/// if \p lower.
static std::vector<mp_integer> characters(const std::string &text, bool lower)
{
  std::vector<mp_integer> result;
  for(const char c : text)
  {
    result.emplace_back(
      lower && 'A' <= c && c <= 'Z' ? c + 0x20 : static_cast<unsigned char>(c));
  }
  return result;
}

/// Whether an encoded argument is of one of \p kinds.
static bool eval_kind_is_one_of(
  const std::vector<mp_integer> &arg,
  const std::string &kinds)
{
  for(const char k : kinds)
  {
    if(arg[0] == k)
      return true;
  }
  return false;
}

/// Return the string to replace the format specifier, as a vector of
/// characters, or an empty optional if it cannot be computed.
static std::optional<std::vector<mp_integer>> eval_format_specifier(
  const format_specifiert &fs,
  const std::vector<mp_integer> &arg)
{
  switch(fs.conversion)
  {
  case format_specifiert::DECIMAL_INTEGER:
  {
    if(eval_is_null(arg))
      return std::vector<mp_integer>{'n', 'u', 'l', 'l'};
    if(!eval_kind_is_one_of(arg, integral_kinds))
      return {};
    return deserialize_constant_int_arg(arg, 10);
  }
  case format_specifiert::HEXADECIMAL_INTEGER:
  case format_specifiert::OCTAL_INTEGER:
  {
    if(eval_is_null(arg))
      return std::vector<mp_integer>{'n', 'u', 'l', 'l'};
    if(!eval_kind_is_one_of(arg, integral_kinds))
      return {};
    // the unsigned value of the type of the argument (see unsigned_value)
    mp_integer value = eval_long_arg(arg);
    const std::size_t width = arg[0] == kind_byte    ? 8
                              : arg[0] == kind_short ? 16
                              : arg[0] == kind_int   ? 32
                                                     : 64;
    if(value < 0)
      value += power(2, width);
    const unsigned radix =
      fs.conversion == format_specifiert::OCTAL_INTEGER ? 8 : 16;
    // integer2string gives upper-case digits
    return characters(integer2string(value, radix), true);
  }
  case format_specifiert::HEXADECIMAL_INTEGER_UPPER:
  {
    format_specifiert fs_lower = fs;
    fs_lower.conversion = format_specifiert::HEXADECIMAL_INTEGER;
    auto lower_case = eval_format_specifier(fs_lower, arg);
    if(!lower_case.has_value())
      return {};
    return make_range(*lower_case)
      .map(
        [](const mp_integer &c)
        {
          if('a' <= c && c <= 'z')
            return c - 0x20;
          return c;
        });
  }
  case format_specifiert::SCIENTIFIC:
  case format_specifiert::DECIMAL_FLOAT:
  case format_specifiert::GENERAL:
  case format_specifiert::HEXADECIMAL_FLOAT:
  {
    if(eval_is_null(arg))
      return std::vector<mp_integer>{'n', 'u', 'l', 'l'};
    if(arg[0] != kind_float && arg[0] != kind_double)
      return {};
    const auto bits = static_cast<std::uint64_t>(eval_long_arg(arg).to_long());
    double value;
    std::memcpy(&value, &bits, sizeof(value));
    return characters(
      java_format_float(value, fs.conversion, fs.precision), false);
  }
  case format_specifiert::CHARACTER:
  {
    if(eval_is_null(arg))
      return std::vector<mp_integer>{'n', 'u', 'l', 'l'};
    if(arg[0] == kind_char)
      return std::vector<mp_integer>{arg[4]};
    if(!eval_kind_is_one_of(arg, std::string{kind_byte, kind_short, kind_int}))
      return {};
    const mp_integer code_point = eval_long_arg(arg);
    if(code_point < 0 || code_point > 0x10FFFF)
      return {};
    if(code_point <= 0xFFFF)
      return std::vector<mp_integer>{code_point};
    const mp_integer offset = code_point - 0x10000;
    return std::vector<mp_integer>{
      0xD800 + offset / 1024, 0xDC00 + offset % 1024};
  }
  case format_specifiert::BOOLEAN:
  {
    if(eval_is_null(arg) || (arg[0] == kind_boolean && arg[4] == 0))
      return std::vector<mp_integer>{'f', 'a', 'l', 's', 'e'};
    return std::vector<mp_integer>{'t', 'r', 'u', 'e'};
  }
  case format_specifiert::STRING:
    return std::vector<mp_integer>{arg.begin() + text_offset, arg.end()};
  case format_specifiert::HASHCODE:
  {
    if(eval_is_null(arg))
      return std::vector<mp_integer>{'n', 'u', 'l', 'l'};
    if((arg[properties_offset].to_ulong() & unknown_hash_property) != 0)
      return {};
    const mp_integer hash = arg[hash_offset] * 0x10000 + arg[hash_offset + 1];
    return characters(integer2string(hash, 16), true);
  }
  case format_specifiert::LINE_SEPARATOR:
    // TODO: the constant should depend on the system: System.lineSeparator()
    return std::vector<mp_integer>{'\n'};
  case format_specifiert::PERCENT_SIGN:
    return std::vector<mp_integer>{'%'};
  case format_specifiert::SCIENTIFIC_UPPER:
  case format_specifiert::GENERAL_UPPER:
  case format_specifiert::HEXADECIMAL_FLOAT_UPPER:
  case format_specifiert::CHARACTER_UPPER:
  case format_specifiert::DATE_TIME_UPPER:
  case format_specifiert::BOOLEAN_UPPER:
  case format_specifiert::STRING_UPPER:
  case format_specifiert::HASHCODE_UPPER:
  {
    format_specifiert fs_lower = fs;
    fs_lower.conversion = tolower(fs.conversion);
    auto lower_case = eval_format_specifier(fs_lower, arg);
    if(!lower_case.has_value())
      return {};
    // (only the upper case of ASCII is computed here)
    for(const mp_integer &c : *lower_case)
    {
      if(c >= 0x80)
        return {};
    }
    return make_range(*lower_case)
      .map(
        [](const mp_integer &c)
        {
          if('a' <= c && c <= 'z')
            return c - 0x20;
          return c;
        });
  }
  case format_specifiert::DATE_TIME:
  {
    if(eval_is_null(arg))
      return std::vector<mp_integer>{'n', 'u', 'l', 'l'};
    if(
      arg[0] != kind_long ||
      (fs.date_time_field != 'Q' && fs.date_time_field != 's'))
    {
      return {};
    }
    mp_integer value = eval_long_arg(arg);
    // (rounded towards zero, see epoch_value)
    if(fs.date_time_field == 's')
      value /= 1000;
    return characters(integer2string(value, 10), false);
  }
  }

  INVARIANT(false, "format specifier must belong to [bBhHsScCdoxXeEfgGaAtT%n]");
}

std::optional<exprt> string_format_builtin_functiont::eval(
  const std::function<exprt(const exprt &)> &get_value) const
{
  if(!format_string.has_value())
    return {};

  const std::vector<format_elementt> format_strings =
    parse_format_string(format_string.value());
  std::vector<mp_integer> result_vector;

  for(const format_elementt &fe : format_strings)
  {
    if(fe.is_format_specifier())
    {
      const format_specifiert &fs = fe.get_format_specifier();
      if(!fs.is_modelled())
        return {};
      if(takes_argument(fs))
      {
        const auto position = argument_position(fs, inputs.size());
        if(!position.has_value())
          return {};
        auto arg_value = eval_string(inputs[*position], get_value);
        // (a missing argument is "", and Java throws)
        if(!arg_value.has_value() || arg_value->size() < text_offset)
          return {};
        auto evaluated = eval_format_specifier(fs, *arg_value);
        if(!evaluated.has_value())
          return {};
        std::vector<mp_integer> evaluated_char_vector = std::move(*evaluated);
        std::move(
          evaluated_char_vector.begin(),
          evaluated_char_vector.end(),
          std::back_inserter(result_vector));
      }
      else if(fs.conversion == format_specifiert::PERCENT_SIGN)
      {
        result_vector.push_back('%');
      }
      else
      {
        // TODO: the character should depend on the system:
        // System.lineSeparator()
        result_vector.push_back('\n');
      }
    }
    else
    {
      for(char c : fe.get_format_text().get_content())
        result_vector.emplace_back(c);
    }
  }
  return make_string(result_vector, to_array_type(result.type()));
}

string_constraintst string_format_builtin_functiont::constraints(
  string_constraint_generatort &generator,
  message_handlert &message_handler) const
{
  // When `format_string` was not set, leave the result non-deterministic
  if(!format_string.has_value())
  {
    generator.approximation_warning(
      "String.format", "is only modelled for a constant format string");
    return {};
  }

  auto result_constraint_pair = add_axioms_for_format(
    generator, result, format_string.value(), inputs, message_handler);
  INVARIANT(
    simplify_expr(result_constraint_pair.first, generator.ns) == 0,
    "add_axioms_for_format should return 0, meaning that formatting was"
    "successful");
  result_constraint_pair.second.existential.push_back(
    equal_exprt{return_code, from_integer(0, get_return_code_type())});
  return result_constraint_pair.second;
}

/// Compute the length of the representation of an integer in a radix, as
/// printed by Java's Long.toString(long, int).
/// \param integer: (not necessarily constant) signed integer for which to
///   compute the length of the representation.
/// \param length_type: type to give to the created expression
/// \param radix: radix of the output representation.
/// \return: An `exprt` representing the length of `integer`
exprt length_of_decimal_int(
  const exprt &integer,
  const typet &length_type,
  const unsigned long radix)
{
  PRECONDITION(integer.type().id() == ID_signedbv);
  PRECONDITION(radix >= 2);
  const std::size_t width = to_signedbv_type(integer.type()).get_width();
  const mp_integer max_value = power(2, width - 1) - 1;

  // The number of digits of the largest magnitude, 2^(width-1).
  std::size_t max_digits = 0;
  for(mp_integer m = max_value + 1; m > 0; m /= radix)
    ++max_digits;

  // Without a sign, x has at most k digits iff x < radix^k, and a negative
  // x has at most k digits after the sign iff x > -radix^k. Comparing with
  // the bound rather than negating x avoids overflow for the minimum.
  exprt positive_length = from_integer(max_digits, length_type);
  exprt negative_length = from_integer(max_digits + 1, length_type);
  for(std::size_t k = max_digits - 1; k >= 1; --k)
  {
    const mp_integer bound = power(radix, k);
    if(bound <= max_value)
    {
      positive_length = if_exprt{
        binary_relation_exprt{
          integer, ID_lt, from_integer(bound, integer.type())},
        from_integer(k, length_type),
        std::move(positive_length)};
    }
    else
      positive_length = from_integer(k, length_type);
    negative_length = if_exprt{
      binary_relation_exprt{
        integer, ID_gt, from_integer(-bound, integer.type())},
      from_integer(k + 1, length_type),
      std::move(negative_length)};
  }

  return if_exprt{
    binary_relation_exprt{integer, ID_ge, from_integer(0, integer.type())},
    std::move(positive_length),
    std::move(negative_length)};
}

/// Return an expression representing the length of the format specifier
/// Does not assume that arg is constant.
/// \return the length, or an empty optional for specifiers whose length has
///   no formula here; the length of the result is then only constrained by
///   the content axioms of the format function.
std::optional<exprt> length_for_format_specifier(
  const format_specifiert &fs,
  const array_string_exprt &arg,
  const typet &index_type,
  array_poolt &array_pool)
{
  // any length, for what is not modelled (for a specifier with an argument)
  const auto unknown_length = [&]()
  {
    return array_pool.get_or_create_length(array_pool.fresh_string(
      index_type, to_type_with_subtype(arg.content().type()).subtype()));
  };
  switch(fs.conversion)
  {
  case format_specifiert::DECIMAL_INTEGER:
  {
    return if_exprt{
      is_null(arg, array_pool),
      from_integer(4, index_type),
      if_exprt{
        kind_is_one_of(arg, integral_kinds),
        length_of_decimal_int(
          format_arg_from_string(arg, ID_int, array_pool), index_type, 10),
        unknown_length()}};
  }
  case format_specifiert::HEXADECIMAL_INTEGER:
  case format_specifiert::HEXADECIMAL_INTEGER_UPPER:
  case format_specifiert::OCTAL_INTEGER:
  {
    const unsigned radix =
      fs.conversion == format_specifiert::OCTAL_INTEGER ? 8 : 16;
    return if_exprt{
      is_null(arg, array_pool),
      from_integer(4, index_type),
      if_exprt{
        kind_is_one_of(arg, integral_kinds),
        length_of_decimal_int(
          unsigned_value(arg, array_pool), index_type, radix),
        unknown_length()}};
  }
  case format_specifiert::HASHCODE:
  case format_specifiert::HASHCODE_UPPER:
    return if_exprt{
      is_null(arg, array_pool),
      from_integer(4, index_type),
      if_exprt{
        hash_is_known(arg),
        length_of_decimal_int(hash_value(arg), index_type, 16),
        unknown_length()}};
  case format_specifiert::SCIENTIFIC:
  case format_specifiert::DECIMAL_FLOAT:
    return {};
  case format_specifiert::CHARACTER:
  case format_specifiert::CHARACTER_UPPER:
  {
    // "null", one char, or a surrogate pair for a code point beyond the
    // basic plane
    const exprt value = format_arg_from_string(arg, ID_int, array_pool);
    const exprt code_point =
      kind_is_one_of(arg, std::string{kind_byte, kind_short, kind_int});
    const exprt basic = and_exprt{
      binary_relation_exprt{value, ID_ge, from_integer(0, value.type())},
      binary_relation_exprt{value, ID_le, from_integer(0xFFFF, value.type())}};
    const exprt supplementary = and_exprt{
      binary_relation_exprt{value, ID_gt, from_integer(0xFFFF, value.type())},
      binary_relation_exprt{
        value, ID_le, from_integer(0x10FFFF, value.type())}};
    return if_exprt{
      is_null(arg, array_pool),
      from_integer(4, index_type),
      if_exprt{
        or_exprt{kind_is(arg, kind_char), and_exprt{code_point, basic}},
        from_integer(1, index_type),
        if_exprt{
          and_exprt{code_point, supplementary},
          from_integer(2, index_type),
          unknown_length()}}};
  }
  case format_specifiert::BOOLEAN:
  case format_specifiert::BOOLEAN_UPPER:
  {
    return if_exprt{format_arg_from_string(arg, ID_boolean, array_pool),
                    from_integer(4, index_type),
                    from_integer(5, index_type)};
  }
  case format_specifiert::STRING:
  case format_specifiert::STRING_UPPER:
    // the text after the kind and the value
    return minus_exprt{
      array_pool.get_or_create_length(arg),
      from_integer(text_offset, index_type)};
  case format_specifiert::LINE_SEPARATOR:
    // TODO: the constant should depend on the system: System.lineSeparator()
    return from_integer(1, index_type);
  case format_specifiert::PERCENT_SIGN:
    return from_integer(1, index_type);
  case format_specifiert::SCIENTIFIC_UPPER:
  case format_specifiert::GENERAL_UPPER:
  case format_specifiert::HEXADECIMAL_FLOAT_UPPER:
  case format_specifiert::DATE_TIME_UPPER:
  {
    // Upper-casing preserves the length.
    format_specifiert fs_lower = fs;
    fs_lower.conversion = tolower(fs.conversion);
    return length_for_format_specifier(fs_lower, arg, index_type, array_pool);
  }
  case format_specifiert::GENERAL:
  case format_specifiert::HEXADECIMAL_FLOAT:
  case format_specifiert::DATE_TIME:
    // No formula: the content axioms constrain the length.
    return {};
  }

  INVARIANT(false, "format specifier must belong to [bBhHsScCdoxXeEfgGaAtT%n]");
}

bool string_format_builtin_functiont::length_is_exact() const
{
  if(!format_string.has_value())
    return false;
  for(const format_elementt &fe : parse_format_string(format_string.value()))
  {
    if(!fe.is_format_specifier())
      continue;
    const format_specifiert &fs = fe.get_format_specifier();
    if(!fs.is_modelled())
      return false;
    if(!takes_argument(fs))
      continue;
    const auto position = argument_position(fs, inputs.size());
    if(!position.has_value())
      return false;
    // (Where the length is a formula of the argument for some kinds only,
    // it is any length for the others, as in the full constraints.)
    switch(fs.conversion)
    {
    case format_specifiert::SCIENTIFIC:
    case format_specifiert::SCIENTIFIC_UPPER:
    case format_specifiert::DECIMAL_FLOAT:
    case format_specifiert::GENERAL:
    case format_specifiert::GENERAL_UPPER:
    case format_specifiert::HEXADECIMAL_FLOAT:
    case format_specifiert::HEXADECIMAL_FLOAT_UPPER:
    case format_specifiert::DATE_TIME:
    case format_specifiert::DATE_TIME_UPPER:
      // No length formula.
      return false;
    default:
      break;
    }
  }
  return true;
}

exprt string_format_builtin_functiont::length_constraint() const
{
  if(!format_string.has_value())
    return true_exprt{};

  exprt::operandst constraints;
  const std::vector<format_elementt> format_strings =
    parse_format_string(format_string.value());
  std::vector<exprt> intermediary_string_lengths;
  const typet &index_type = result.length_type();

  for(const format_elementt &fe : format_strings)
  {
    if(fe.is_format_specifier())
    {
      const format_specifiert &fs = fe.get_format_specifier();
      const auto position = argument_position(fs, inputs.size());
      array_string_exprt arg;
      if(position.has_value())
        arg = inputs[*position];
      std::optional<exprt> length;
      if(fs.is_modelled() && (!takes_argument(fs) || position.has_value()))
        length = length_for_format_specifier(fs, arg, index_type, array_pool);
      if(!length.has_value())
      {
        // Leave the length of the result to the content axioms.
        return equal_exprt{
          return_code, from_integer(0, get_return_code_type())};
      }
      intermediary_string_lengths.push_back(std::move(*length));
    }
    else
    {
      intermediary_string_lengths.push_back(from_integer(
        fe.get_format_text().get_content().size(), result.length_type()));
    }
  }

  constraints.push_back(
    equal_exprt{return_code, from_integer(0, get_return_code_type())});

  if(intermediary_string_lengths.empty())
  {
    constraints.push_back(equal_exprt(
      array_pool.get_or_create_length(result), from_integer(0, index_type)));
    return conjunction(constraints);
  }

  exprt total_length = intermediary_string_lengths[0];
  for(std::size_t i = 1; i < intermediary_string_lengths.size(); ++i)
  {
    total_length =
      plus_exprt{std::move(total_length), intermediary_string_lengths[i]};
  }
  constraints.push_back(equal_exprt{array_pool.get_or_create_length(result),
                                    std::move(total_length)});

  return conjunction(constraints);
}

/// The text of an error of String.format (see add_axioms_for_format_error).
using format_error_textt = std::vector<mp_integer>;

/// Append \p value to \p text as two characters, the most significant 16
/// bits first.
static void append_int(format_error_textt &text, int value)
{
  const auto bits = static_cast<std::uint32_t>(value);
  text.emplace_back(bits >> 16);
  text.emplace_back(bits & 0xFFFF);
}

/// Append the characters of \p s to \p text.
static void append_string(format_error_textt &text, const std::string &s)
{
  for(const char c : s)
    text.emplace_back(static_cast<unsigned char>(c));
}

/// The error text of an error of the parse (see java_format_parse).
static format_error_textt
parse_error_text(const std::pair<char, std::string> &error)
{
  format_error_textt text{error.first};
  switch(error.first)
  {
  case java_format_errort::argument_index:
  case java_format_errort::width:
  case java_format_errort::precision:
    append_int(text, std::stoi(error.second));
    break;
  case java_format_errort::flags_mismatch:
    // the conversion, no argument, the flags
    text.emplace_back(static_cast<unsigned char>(error.second[0]));
    append_int(text, 0);
    append_string(text, error.second.substr(1));
    break;
  default:
    append_string(text, error.second);
  }
  return text;
}

/// The errors that the print step of java.util.Formatter may raise for the
/// specifier \p fs, as pairs of a condition and the text of the error (see
/// add_axioms_for_format_error), in the order of Formatter. \p n is the
/// number of arguments and \p args the encoded ones (see kind_null).
/// \param generator: constraint generator, for fresh symbols
static std::vector<std::pair<exprt, format_error_textt>> print_errors(
  string_constraint_generatort &generator,
  const java_format_specifiert &fs,
  const exprt &n,
  const std::vector<array_string_exprt> &args,
  java_formatter_versiont version)
{
  namespace error = java_format_errort;
  using namespace java_format_flagst;
  std::vector<std::pair<exprt, format_error_textt>> errors;
  format_error_textt missing_text{error::missing_argument};
  append_string(missing_text, fs.text);
  if(fs.argument == 0)
  {
    // a `<` without a previous argument
    errors.emplace_back(true_exprt{}, missing_text);
    return errors;
  }
  errors.emplace_back(
    binary_relation_exprt{n, ID_lt, from_integer(fs.argument, n.type())},
    missing_text);

  // The text of an error with the argument.
  const auto with_argument = [&](char code, const std::string &detail)
  {
    format_error_textt text{code};
    append_string(text, detail);
    append_int(text, fs.argument);
    return text;
  };
  const char c = fs.conversion;
  const std::string conversion{c};
  const int bad_flags =
    (c == 'o' || c == 'x')
      ? java_format_bad_flags(
          fs.flags, {parentheses, leading_space, plus}, version)
      : 0;
  const bool alternate_string = c == 's' && (fs.flags & alternate) != 0;
  // The argument, where the arguments of the model have it: beyond them, any
  // of the errors may occur, and the model checks the one that does.
  std::optional<array_string_exprt> arg;
  if(fs.argument <= static_cast<int>(args.size()))
    arg = args[fs.argument - 1];
  // whether an error beyond the arguments of the model occurs
  const auto beyond = [&]()
  { return generator.fresh_symbol("format_error_beyond", bool_typet{}); };

  if(fs.date_time)
  {
    // Date, Calendar and Long fit, and a TemporalAccessor where it has the
    // field, which is not modelled.
    exprt mismatch = beyond();
    if(arg.has_value())
    {
      mismatch = or_exprt{
        not_exprt{kind_is_one_of(
          *arg,
          std::string{kind_null, kind_long, kind_calendar, kind_temporal})},
        and_exprt{
          kind_is(*arg, kind_temporal),
          generator.fresh_symbol("format_temporal_field", bool_typet{})}};
    }
    errors.emplace_back(
      mismatch, with_argument(error::date_time_mismatch, conversion));
    return errors;
  }

  std::string fits;
  switch(c)
  {
  case 'c':
    fits = {kind_null, kind_char, kind_byte, kind_short, kind_int};
    break;
  case 'd':
  case 'o':
  case 'x':
    fits = integral_kinds + kind_null + kind_big_integer;
    break;
  case 'e':
  case 'f':
  case 'g':
    fits = {kind_null, kind_float, kind_double, kind_big_decimal};
    break;
  case 'a':
    fits = {kind_null, kind_float, kind_double};
    break;
  default:
    break;
  }
  if(!fits.empty())
  {
    errors.emplace_back(
      arg.has_value() ? exprt{not_exprt{kind_is_one_of(*arg, fits)}} : beyond(),
      with_argument(error::conversion_mismatch, conversion));
  }
  if(c == 'c')
  {
    exprt invalid = beyond();
    if(arg.has_value())
    {
      const exprt value =
        format_arg_from_string(*arg, ID_int, generator.array_pool);
      invalid = and_exprt{
        kind_is_one_of(*arg, std::string{kind_byte, kind_short, kind_int}),
        or_exprt{
          binary_relation_exprt{value, ID_lt, from_integer(0, value.type())},
          binary_relation_exprt{
            value, ID_gt, from_integer(0x10FFFF, value.type())}}};
    }
    errors.emplace_back(invalid, with_argument(error::code_point, ""));
  }
  if(alternate_string || bad_flags != 0)
  {
    // `#` for a %s of an argument that is not a Formattable, and the flags
    // `( +` for a %o or %x of a Byte, Short, Integer or Long
    exprt mismatch = beyond();
    if(arg.has_value() && alternate_string)
    {
      const typet &char_type =
        to_type_with_subtype(arg->content().type()).subtype();
      const exprt properties =
        (*arg)[from_integer(properties_offset, arg->length_type())];
      mismatch = not_exprt{and_exprt{
        kind_is_one_of(*arg, object_kinds),
        notequal_exprt{
          bitand_exprt{
            properties, from_integer(formattable_property, char_type)},
          from_integer(0, char_type)}}};
    }
    else if(arg.has_value())
      mismatch = kind_is_one_of(*arg, integral_kinds);
    format_error_textt text = with_argument(error::flags_mismatch, conversion);
    append_string(text, to_string(alternate_string ? alternate : bad_flags));
    errors.emplace_back(mismatch, text);
  }
  return errors;
}

/// The first error of Java's String.format with a format string and its
/// arguments, in the order of java.util.Formatter: the errors of its parse,
/// then those of each specifier in turn as it is printed. Where the Java
/// runtimes that may run the program (see configt::javat) parse it
/// differently (see java_formatter_versiont), any of their errors. The result is "" for none, or the code of the error (see
/// java_format_errort) and its detail:
/// - for an error of the parse, the detail of java_format_parse, with the
///   index, width or precision as two characters (the most significant 16
///   bits first), except for flags that do not fit the conversion;
/// - for a missing argument, the specifier;
/// - for a conversion that does not fit its argument, the conversion (for a
///   date/time, its field) and the argument (from 1, as two characters);
/// - for an invalid code point, the argument;
/// - for flags that do not fit the conversion, the conversion, the argument
///   (0 for an error of the parse) and the flags.
/// For a format string that is not constant, the result is unknown, and so
/// it is for the errors of the arguments beyond those passed; the model
/// checks such an error with the argument itself.
/// \param f: function application with the result (its length and
///   content), the format string, the number of arguments and the first ten
///   arguments, encoded (see kind_null)
/// \return a return code
std::pair<exprt, string_constraintst>
string_constraint_generatort::add_axioms_for_format_error(
  const function_application_exprt &f)
{
  PRECONDITION(f.arguments().size() == 14);
  string_constraintst constraints;
  const array_string_exprt res =
    array_pool.find(f.arguments()[1], f.arguments()[0]);
  const array_string_exprt str = get_string_expr(array_pool, f.arguments()[2]);
  const exprt &n = f.arguments()[3];
  std::vector<array_string_exprt> args;
  for(std::size_t i = 4; i < f.arguments().size(); ++i)
    args.push_back(get_string_expr(array_pool, f.arguments()[i]));
  const exprt return_code = from_integer(0, f.type());
  const exprt length = array_pool.get_or_create_length(str);
  if(!length.is_constant() || str.content().id() != ID_array)
  {
    approximation_warning(
      "String.format", "only checks a constant format string");
    return {return_code, constraints};
  }
  const auto size = numeric_cast_v<std::size_t>(to_constant_expr(length));
  const std::string format =
    utf16_constant_array_to_java(to_array_expr(str.content()), size);

  const typet &index_type = res.length_type();
  const typet &char_type = to_type_with_subtype(res.content().type()).subtype();
  const auto res_is = [&](const format_error_textt &text)
  {
    exprt::operandst conjuncts{equal_exprt{
      array_pool.get_or_create_length(res),
      from_integer(text.size(), index_type)}};
    for(std::size_t i = 0; i < text.size(); ++i)
    {
      conjuncts.push_back(equal_exprt{
        res[from_integer(i, index_type)], from_integer(text[i], char_type)});
    }
    return conjunction(conjuncts);
  };
  // The errors of each version of Formatter of the runtimes that may run
  // the program, of which one, any, gives the result.
  const std::vector<java_formatter_versiont> versions =
    java_formatter_versions();
  const symbol_exprt chosen =
    fresh_symbol("format_error_version", unsignedbv_typet{8});
  constraints.existential.push_back(binary_relation_exprt{
    chosen, ID_lt, from_integer(versions.size(), chosen.type())});
  for(std::size_t v = 0; v < versions.size(); ++v)
  {
    const exprt this_version =
      versions.size() == 1
        ? exprt{true_exprt{}}
        : exprt{equal_exprt{chosen, from_integer(v, chosen.type())}};
    const java_format_parset parsed = java_format_parse(format, versions[v]);
    if(parsed.error.has_value())
    {
      constraints.existential.push_back(
        implies_exprt{this_version, res_is(parse_error_text(*parsed.error))});
      continue;
    }
    // The first error whose condition holds gives the result.
    exprt earlier = false_exprt{};
    for(const java_format_specifiert &fs : parsed.specifiers)
    {
      if(!fs.takes_argument)
        continue;
      for(auto &error : print_errors(*this, fs, n, args, versions[v]))
      {
        constraints.existential.push_back(implies_exprt{
          and_exprt{this_version, not_exprt{earlier}, error.first},
          res_is(error.second)});
        earlier = or_exprt{earlier, error.first};
      }
    }
    constraints.existential.push_back(
      implies_exprt{and_exprt{this_version, not_exprt{earlier}}, res_is({})});
  }
  return {return_code, constraints};
}

/// The calls of toString and hashCode that the arguments of String.format
/// need for a format string, as bits: bit 2i for the toString of the
/// argument i (from 0) for a %s, bit 2i+1 for its hashCode for a %h, for
/// the first ten arguments. Formatter calls none for an error of the parse.
/// For a format string that is not constant, the result is unknown.
/// \param f: function application with the format string
/// \return an integer expression
std::pair<exprt, string_constraintst>
string_constraint_generatort::add_axioms_for_format_uses(
  const function_application_exprt &f)
{
  PRECONDITION(f.arguments().size() == 1);
  const array_string_exprt str = get_string_expr(array_pool, f.arguments()[0]);
  const exprt length = array_pool.get_or_create_length(str);
  if(!length.is_constant() || str.content().id() != ID_array)
    return {fresh_symbol("format_uses", f.type()), {}};
  const auto size = numeric_cast_v<std::size_t>(to_constant_expr(length));
  const std::string format =
    utf16_constant_array_to_java(to_array_expr(str.content()), size);
  // (those of any of the versions of Formatter that may run the program)
  std::uint32_t uses = 0;
  for(const java_formatter_versiont version : java_formatter_versions())
  {
    for(const java_format_specifiert &fs :
        java_format_parse(format, version).specifiers)
    {
      if(!fs.takes_argument || fs.argument < 1 || fs.argument > 10)
        continue;
      if(fs.conversion == 's' && !fs.date_time)
        uses |= std::uint32_t{1} << (2 * (fs.argument - 1));
      else if(fs.conversion == 'h' && !fs.date_time)
        uses |= std::uint32_t{1} << (2 * (fs.argument - 1) + 1);
    }
  }
  return {from_integer(uses, f.type()), {}};
}
