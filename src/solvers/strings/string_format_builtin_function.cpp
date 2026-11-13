/*******************************************************************\

Module: Built-in function for String.format

Author: Romain Brenguier, Joel Allred

\*******************************************************************/

/// \file
///  Built-in function for String.format

#include "string_format_builtin_function.h"

#include <util/bitvector_expr.h>
#include <util/message.h>
#include <util/range.h>
#include <util/simplify_expr.h>
#include <util/symbol_table.h>

#include "format_specifier.h"
#include "string_refinement_util.h"

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
// a character for its kind, four characters for the value of an integral,
// char or boolean as a long (16 bits each, the most significant first), and
// the text of String.valueOf of the argument. A missing argument is "".
static const char kind_null = 'N';
static const char kind_string = 'S';
static const char kind_int = 'I'; // Byte, Short or Integer
static const char kind_long = 'L';
static const char kind_char = 'C';
static const char kind_boolean = 'B';
static const char kind_float = 'F'; // Float or Double
static const char kind_other = 'O';
static const std::size_t text_offset = 5;

/// Whether the argument \p arg is of kind \p kind.
static exprt kind_is(const array_string_exprt &arg, char kind)
{
  return equal_exprt{
    arg[from_integer(0, arg.length_type())],
    from_integer(kind, to_type_with_subtype(arg.content().type()).subtype())};
}

/// Expression which is true when the argument is null
static exprt is_null(const array_string_exprt &string, array_poolt &)
{
  return kind_is(string, kind_null);
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
    string_equals_if(
      generator, not_exprt{null}, res, digits, constraints, message_handler);
    return {res, std::move(constraints)};
  }
  case format_specifiert::HEXADECIMAL_INTEGER:
  {
    // Java prints a negative value as the two's complement of its own type,
    // which the serialisation of the argument as a long does not record, so
    // only a non-negative value determines the result.
    const exprt arg = format_arg_from_string(string_arg, ID_int, array_pool);
    const array_string_exprt digits =
      array_pool.fresh_string(index_type, char_type);
    return_code = generator.add_axioms_for_string_of_int_with_radix(
      digits, arg, from_integer(16, index_type), 16);
    const binary_relation_exprt non_negative{
      arg, ID_ge, from_integer(0, arg.type())};
    if(!simplify_expr(non_negative, generator.ns).is_true())
    {
      generator.approximation_warning(
        "String.format",
        "is not modelled for the format specifier %x of a negative value");
    }
    const exprt null = is_null(string_arg, array_pool);
    return_code.second.existential.push_back(
      string_is_constant_if(null, res, "null", array_pool));
    return_code.second.existential.push_back(implies_exprt{
      and_exprt{not_exprt{null}, non_negative},
      equal_exprt{
        array_pool.get_or_create_length(res),
        array_pool.get_or_create_length(digits)}});
    const symbol_exprt qvar =
      generator.fresh_symbol("QA_format_hex", index_type);
    return_code.second.universal.emplace_back(
      qvar,
      zero_if_negative(array_pool.get_or_create_length(digits)),
      implies_exprt{
        and_exprt{not_exprt{null}, non_negative},
        equal_exprt{res[qvar], digits[qvar]}},
      generator.ns,
      message_handler);
    return {res, std::move(return_code.second)};
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
    const exprt basic =
      binary_relation_exprt{value, ID_le, from_integer(0xFFFF, value.type())};
    const exprt single = and_exprt{
      not_exprt{null}, or_exprt{kind_is(string_arg, kind_char), basic}};
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
      and_exprt{
        not_exprt{null}, kind_is(string_arg, kind_int), not_exprt{basic}},
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
    // Integer.toHexString of the hash code, which is not modelled, or
    // "null".
    generator.approximation_warning(
      "String.format", "is not modelled for the format specifier %h");
    constraints.existential.push_back(string_is_constant_if(
      is_null(string_arg, array_pool), res, "null", array_pool));
    return {res, constraints};
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
  case format_specifiert::SCIENTIFIC:
  case format_specifiert::DECIMAL_FLOAT:
    /// \todo The Java models pass floating-point arguments truncated to a
    /// long, so their value is not available here.
  case format_specifiert::OCTAL_INTEGER:
    /// \todo Conversion of octal is not implemented.
  case format_specifiert::GENERAL:
    /// \todo Conversion for format specifier general is not implemented.
  case format_specifiert::HEXADECIMAL_FLOAT:
    /// \todo Conversion of hexadecimal float is not implemented.
  case format_specifiert::DATE_TIME:
  {
    /// \todo Conversion of date-time is not implemented
    // For all these unimplemented cases we return a non-deterministic string
    generator.approximation_warning(
      "String.format",
      std::string{"is not modelled for the format specifier %"} +
        fs.conversion);
    return {array_pool.fresh_string(index_type, char_type), {}};
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
  for(std::size_t i = 1; i < text_offset; i++)
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
    return deserialize_constant_int_arg(arg, 10);
  }
  case format_specifiert::HEXADECIMAL_INTEGER:
  {
    if(eval_is_null(arg))
      return std::vector<mp_integer>{'n', 'u', 'l', 'l'};
    auto upper_case_hex = deserialize_constant_int_arg(arg, 16);
    // A negative value is printed in the two's complement of a type that the
    // argument does not record.
    if(!upper_case_hex.empty() && upper_case_hex[0] == '-')
      return {};
    // convert to lower case
    return make_range(upper_case_hex).map([](const mp_integer &c) {
      if('A' <= c && c <= 'Z')
        return c + 0x20;
      return c;
    });
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
    return {};
  case format_specifiert::CHARACTER:
  {
    if(eval_is_null(arg))
      return std::vector<mp_integer>{'n', 'u', 'l', 'l'};
    if(arg[0] == kind_char)
      return std::vector<mp_integer>{arg[4]};
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
    return {};
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
    return make_range(*lower_case)
      .map(
        [](const mp_integer &c)
        {
          // TODO: incomplete
          if('a' <= c && c <= 'z')
            return c - 0x20;
          return c;
        });
  }
  case format_specifiert::OCTAL_INTEGER:
    /// \todo Conversion of octal is not implemented.
  case format_specifiert::GENERAL:
    /// \todo Conversion for format specifier general is not implemented.
  case format_specifiert::HEXADECIMAL_FLOAT:
    /// \todo Conversion of hexadecimal float is not implemented.
  case format_specifiert::DATE_TIME:
    /// \todo Conversion of date-time is not implemented
    return {};
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
  switch(fs.conversion)
  {
  case format_specifiert::DECIMAL_INTEGER:
  {
    return if_exprt(
      is_null(arg, array_pool),
      from_integer(4, index_type),
      length_of_decimal_int(
        format_arg_from_string(arg, ID_int, array_pool), index_type, 10));
  }
  case format_specifiert::HEXADECIMAL_INTEGER:
  case format_specifiert::HEXADECIMAL_INTEGER_UPPER:
  {
    // The length of a negative value is not determined, see
    // add_axioms_for_format_specifier.
    const exprt integer = format_arg_from_string(arg, ID_int, array_pool);
    const typet &char_type =
      to_type_with_subtype(arg.content().type()).subtype();
    const exprt non_negative = simplify_expr(
      binary_relation_exprt{integer, ID_ge, from_integer(0, integer.type())},
      namespacet{symbol_tablet{}});
    if(non_negative.is_true())
      return length_of_decimal_int(integer, index_type, 16);
    return if_exprt{
      non_negative,
      length_of_decimal_int(integer, index_type, 16),
      array_pool.get_or_create_length(
        array_pool.fresh_string(index_type, char_type))};
  }
  case format_specifiert::SCIENTIFIC:
  case format_specifiert::DECIMAL_FLOAT:
    return {};
  case format_specifiert::CHARACTER:
  case format_specifiert::CHARACTER_UPPER:
  {
    // "null", one char, or a surrogate pair for a code point beyond the
    // basic plane
    const exprt value = format_arg_from_string(arg, ID_int, array_pool);
    return if_exprt{
      is_null(arg, array_pool),
      from_integer(4, index_type),
      if_exprt{
        and_exprt{
          kind_is(arg, kind_int),
          binary_relation_exprt{
            value, ID_gt, from_integer(0xFFFF, value.type())}},
        from_integer(2, index_type),
        from_integer(1, index_type)}};
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
  case format_specifiert::HASHCODE:
    return {};
  case format_specifiert::LINE_SEPARATOR:
    // TODO: the constant should depend on the system: System.lineSeparator()
    return from_integer(1, index_type);
  case format_specifiert::PERCENT_SIGN:
    return from_integer(1, index_type);
  case format_specifiert::SCIENTIFIC_UPPER:
  case format_specifiert::GENERAL_UPPER:
  case format_specifiert::HEXADECIMAL_FLOAT_UPPER:
  case format_specifiert::DATE_TIME_UPPER:
  case format_specifiert::HASHCODE_UPPER:
  {
    // Upper-casing preserves the length.
    format_specifiert fs_lower = fs;
    fs_lower.conversion = tolower(fs.conversion);
    return length_for_format_specifier(fs_lower, arg, index_type, array_pool);
  }
  case format_specifiert::OCTAL_INTEGER:
    /// \todo Conversion of octal is not implemented.
  case format_specifiert::GENERAL:
    /// \todo Conversion for format specifier general is not implemented.
  case format_specifiert::HEXADECIMAL_FLOAT:
    /// \todo Conversion of hexadecimal float is not implemented.
  case format_specifiert::DATE_TIME:
    /// \todo Conversion of date-time is not implemented
    // For all these unimplemented cases the content is a non-deterministic
    // string.
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
    const std::size_t index = *position;
    switch(fs.conversion)
    {
    case format_specifiert::HEXADECIMAL_INTEGER:
    case format_specifiert::HEXADECIMAL_INTEGER_UPPER:
    {
      // Open for a negative value, see length_for_format_specifier.
      const exprt integer =
        format_arg_from_string(inputs[index], ID_int, array_pool);
      const exprt non_negative = simplify_expr(
        binary_relation_exprt{integer, ID_ge, from_integer(0, integer.type())},
        namespacet{symbol_tablet{}});
      if(!non_negative.is_true())
        return false;
      break;
    }
    case format_specifiert::SCIENTIFIC:
    case format_specifiert::SCIENTIFIC_UPPER:
    case format_specifiert::DECIMAL_FLOAT:
    case format_specifiert::HASHCODE:
    case format_specifiert::HASHCODE_UPPER:
    case format_specifiert::OCTAL_INTEGER:
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

/// Whether a conversion fits an argument, as Java's Formatter checks it, by
/// the kind of the argument. (Whether an object of kind_other fits a
/// numeric or date/time conversion depends on its class, a BigInteger for
/// %d say, which the kind does not record: see add_axioms_for_format_error.)
static exprt
conversion_fits(const format_specifiert &fs, const array_string_exprt &arg)
{
  const auto any_of = [&](std::initializer_list<char> kinds)
  {
    exprt::operandst disjuncts;
    for(const char k : kinds)
      disjuncts.push_back(kind_is(arg, k));
    return disjunction(disjuncts);
  };
  if(fs.dt)
    return any_of({kind_null, kind_long});
  switch(tolower(fs.conversion))
  {
  case format_specifiert::BOOLEAN:
  case format_specifiert::STRING:
  case format_specifiert::HASHCODE:
    return true_exprt{};
  case format_specifiert::CHARACTER:
    return any_of({kind_null, kind_char, kind_int});
  case format_specifiert::DECIMAL_INTEGER:
  case format_specifiert::OCTAL_INTEGER:
  case format_specifiert::HEXADECIMAL_INTEGER:
    return any_of({kind_null, kind_int, kind_long});
  case format_specifiert::SCIENTIFIC:
  case format_specifiert::DECIMAL_FLOAT:
  case format_specifiert::GENERAL:
  case format_specifiert::HEXADECIMAL_FLOAT:
    return any_of({kind_null, kind_float});
  default:
    return true_exprt{};
  }
}

/// The first error of Java's String.format with a format string and the
/// encoded arguments (see kind_null; "" for a missing argument), in the
/// order of java.util.Formatter: an unknown conversion anywhere (-1), then
/// for each specifier in turn a missing argument (-2), a conversion that
/// does not fit its argument (the conversion character), and an invalid
/// code point for %c (-3); 0 for none. For a format string that is not
/// constant, and for conversions that depend on a class that the kind does
/// not record, the result is unknown. Errors of flags, widths and
/// precisions are not modelled.
/// \param f: function application with the format string and ten
///   arguments
/// \return an integer expression
std::pair<exprt, string_constraintst>
string_constraint_generatort::add_axioms_for_format_error(
  const function_application_exprt &f)
{
  PRECONDITION(f.arguments().size() == 11);
  string_constraintst constraints;
  const array_string_exprt str = get_string_expr(array_pool, f.arguments()[0]);
  std::vector<array_string_exprt> args;
  for(std::size_t i = 1; i < f.arguments().size(); ++i)
    args.push_back(get_string_expr(array_pool, f.arguments()[i]));
  const typet &return_type = f.type();
  const exprt length = array_pool.get_or_create_length(str);
  if(!length.is_constant() || str.content().id() != ID_array)
  {
    approximation_warning(
      "String.format", "only checks a constant format string");
    return {fresh_symbol("format_error", return_type), constraints};
  }
  const auto n = numeric_cast_v<std::size_t>(to_constant_expr(length));
  const std::vector<format_elementt> elements = parse_format_string(
    utf16_constant_array_to_java(to_array_expr(str.content()), n));
  for(const format_elementt &fe : elements)
  {
    if(fe.is_format_specifier() && fe.get_format_specifier().unknown_conversion)
      return {from_integer(-1, return_type), constraints};
  }

  // The errors of the specifiers, the last first, so that the first one
  // that fails gives the result.
  exprt result = from_integer(0, return_type);
  for(auto it = elements.rbegin(); it != elements.rend(); ++it)
  {
    if(!it->is_format_specifier())
      continue;
    const format_specifiert &fs = it->get_format_specifier();
    if(!takes_argument(fs))
      continue;
    if(fs.unsupported || fs.index < 1 || fs.index > 10)
    {
      // `%<` without a previous argument, or beyond the arguments of the
      // model
      result = from_integer(-2, return_type);
      continue;
    }
    const array_string_exprt &arg = args[fs.index - 1];
    const exprt missing = equal_exprt{
      array_pool.get_or_create_length(arg), from_integer(0, arg.length_type())};
    exprt fits = conversion_fits(fs, arg);
    // an object whose class is not recorded may fit a numeric or date/time
    // conversion
    const char lower = static_cast<char>(tolower(fs.conversion));
    if(
      fs.dt || lower == 'd' || lower == 'o' || lower == 'x' || lower == 'e' ||
      lower == 'f' || lower == 'g' || lower == 'a')
    {
      fits = or_exprt{
        fits, and_exprt{kind_is(arg, kind_other), fresh_symbol("format_fits")}};
    }
    result = if_exprt{
      missing,
      from_integer(-2, return_type),
      if_exprt{
        not_exprt{fits},
        from_integer(fs.dt ? 't' : fs.conversion, return_type),
        result}};
    if(lower == 'c')
    {
      // a Byte, Short or Integer must be a valid code point
      const exprt value = format_arg_from_string(arg, ID_int, array_pool);
      const exprt invalid = and_exprt{
        kind_is(arg, kind_int),
        or_exprt{
          binary_relation_exprt{value, ID_lt, from_integer(0, value.type())},
          binary_relation_exprt{
            value, ID_gt, from_integer(0x10FFFF, value.type())}}};
      result = if_exprt{
        and_exprt{not_exprt{missing}, fits, invalid},
        from_integer(-3, return_type),
        result};
    }
  }
  return {result, constraints};
}
