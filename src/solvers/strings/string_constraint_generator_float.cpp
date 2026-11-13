/*******************************************************************\

Module: Generates string constraints for functions generating strings
        from other types, in particular int, long, float, double, char, bool

Author: Romain Brenguier, romain.brenguier@diffblue.com

\*******************************************************************/

/// \file
/// Generates string constraints for functions generating strings from other
/// types, in particular int, long, float, double, char, bool

#include <util/arith_tools.h>
#include <util/floatbv_expr.h>
#include <util/ieee_float.h>
#include <util/mathematical_expr.h>
#include <util/simplify_expr.h>

#include "string_constraint_generator.h"

#include <algorithm>
#include <cmath>

/// A floating-point constant of \p type (a float or a double).
static exprt float_constant(double value, const floatbv_typet &type)
{
  ieee_float_valuet result;
  if(ieee_float_spect{type} == ieee_float_spect::single_precision())
    result.from_float(static_cast<float>(value));
  else
    result.from_double(value);
  return result.to_expr();
}

/// Axioms that \p res equals the constant \p text where \p condition holds.
static exprt string_is(
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
  return conjunction(conjuncts);
}

/// Axioms that hold for format_float of every value, which decide many
/// properties of the strings of symbolic values without checking one value
/// per model, and keep the strings of the models close enough to those of
/// floats that check_float_conversions can exclude them by their strings
/// (validated against format_float on 8.5 million values):
/// - the length is within float_string_length_bounds;
/// - NaN, the infinities and the zeros have their strings;
/// - in the signed formats, the string starts with '-' exactly for a
///   negative value other than NaN;
/// - other values start, after the sign, and end with a digit;
/// - in the range of plain notation, the length is at most that of the
///   significant digits and the zeros and point around them;
/// - after the sign, a magnitude in [k, k + 1) for k from 1 to 9 starts
///   with "k.", and one in the plain range below 1 with "0.".
string_constraintst string_constraint_generatort::float_string_axioms(
  const array_string_exprt &res,
  const exprt &value,
  float_formatt format)
{
  string_constraintst constraints;
  const floatbv_typet &type = to_floatbv_type(value.type());
  const bool single =
    ieee_float_spect{type} == ieee_float_spect::single_precision();
  const typet &index_type = res.length_type();
  const typet &char_type = to_type_with_subtype(res.content().type()).subtype();
  const exprt length = array_pool.get_or_create_length(res);
  const bool python = format == float_formatt::PYTHON;
  const bool signed_format =
    format == float_formatt::JAVA || format == float_formatt::PYTHON;

  const auto bounds = float_string_length_bounds(single, format);
  // The characters are ASCII (digits, signs, the point, the exponent mark,
  // and the letters of inf and nan).
  {
    const symbol_exprt qvar = fresh_symbol("QA_float_ascii", index_type);
    constraints.universal.emplace_back(
      qvar,
      zero_if_negative(length),
      binary_relation_exprt{res[qvar], ID_lt, from_integer(128, char_type)},
      ns,
      message_handler);
  }
  constraints.existential.push_back(and_exprt{
    binary_relation_exprt{
      length, ID_ge, from_integer(bounds.first, index_type)},
    binary_relation_exprt{
      length, ID_le, from_integer(bounds.second, index_type)}});

  const isnan_exprt is_nan{value};
  const isinf_exprt is_inf{value};
  const sign_exprt negative{value};
  const auto special = [&](double v)
  { return format_float(v, single, format); };
  constraints.existential.push_back(
    implies_exprt{is_nan, string_is(res, special(std::nan("")), array_pool)});
  constraints.existential.push_back(implies_exprt{
    and_exprt{is_inf, not_exprt{negative}},
    string_is(res, special(HUGE_VAL), array_pool)});
  constraints.existential.push_back(implies_exprt{
    and_exprt{is_inf, negative},
    string_is(res, special(-HUGE_VAL), array_pool)});
  const exprt zero = float_constant(0.0, type);
  const exprt minus_zero = float_constant(-0.0, type);
  constraints.existential.push_back(implies_exprt{
    equal_exprt{value, zero}, string_is(res, special(0.0), array_pool)});
  constraints.existential.push_back(implies_exprt{
    equal_exprt{value, minus_zero}, string_is(res, special(-0.0), array_pool)});

  const exprt minus = from_integer('-', char_type);
  const exprt has_minus = equal_exprt{res[from_integer(0, index_type)], minus};
  exprt offset = from_integer(0, index_type);
  if(signed_format)
  {
    const and_exprt is_negative{negative, not_exprt{is_nan}};
    constraints.existential.push_back(equal_exprt{has_minus, is_negative});
    offset = if_exprt{is_negative, from_integer(1, index_type), offset};
  }
  else
    constraints.existential.push_back(not_exprt{has_minus});

  // |value| in [lower, upper)
  const auto magnitude_in = [&](double lower, double upper)
  {
    return or_exprt{
      and_exprt{
        binary_relation_exprt{value, ID_ge, float_constant(lower, type)},
        binary_relation_exprt{value, ID_lt, float_constant(upper, type)}},
      and_exprt{
        binary_relation_exprt{value, ID_le, float_constant(-lower, type)},
        binary_relation_exprt{value, ID_gt, float_constant(-upper, type)}}};
  };
  const exprt point = from_integer('.', char_type);
  const plus_exprt after_first{offset, from_integer(1, index_type)};

  // The strings of other values start, after the sign, and end with a
  // digit. (Also constraining every character to the set of the format,
  // and the position of the exponent mark, made properties of prefixes take
  // 115-150 s rather than 13-22 s, and was not needed to decide them.)
  const exprt finite_nonzero = conjunction(
    {not_exprt{is_nan},
     not_exprt{is_inf},
     notequal_exprt{value, zero},
     notequal_exprt{value, minus_zero}});
  const auto is_digit = [&](const exprt &c)
  {
    return and_exprt{
      binary_relation_exprt{c, ID_ge, from_integer('0', char_type)},
      binary_relation_exprt{c, ID_le, from_integer('9', char_type)}};
  };
  constraints.existential.push_back(implies_exprt{
    finite_nonzero,
    and_exprt{
      is_digit(res[offset]),
      is_digit(res[minus_exprt{length, from_integer(1, index_type)}])}});

  if(format == float_formatt::JAVA_SCIENTIFIC)
  {
    // d.d...E[-]e: a non-zero leading digit, the point, a digit, and at
    // least "E" and a digit after them.
    const exprt one_based = from_integer('1', char_type);
    constraints.existential.push_back(implies_exprt{
      finite_nonzero,
      conjunction(
        {binary_relation_exprt{res[offset], ID_ge, one_based},
         binary_relation_exprt{
           res[offset], ID_le, from_integer('9', char_type)},
         equal_exprt{res[after_first], point},
         is_digit(res[plus_exprt{after_first, from_integer(1, index_type)}]),
         binary_relation_exprt{
           length, ID_ge, plus_exprt{offset, from_integer(5, index_type)}}})});
    return constraints;
  }

  // A value in the range of plain notation of the language has, after the
  // sign, at most the significant digits, "0." and the zeros before them
  // (two in Java, three in Python), or 16 integer digits and ".0" (Python).
  // Measured on 2 million values each: 21 characters for doubles and 12 for
  // floats in Java, 22 in Python. (JAVA_PLAIN is given the magnitude of
  // values of all ranges, but used for those of this range.)
  {
    // (Java 18 and earlier takes 18 digits only for doubles of at least
    // 2^53, beyond this range, see float_string_length_bounds.)
    const std::size_t digits = single ? 9 : 17;
    const exprt plain_range =
      python ? magnitude_in(1e-4, 1e16) : magnitude_in(1e-3, 1e7);
    const std::size_t longest =
      (python ? std::max(5 + digits, std::size_t{18}) : 4 + digits) +
      (signed_format ? 1 : 0);
    constraints.existential.push_back(implies_exprt{
      plain_range,
      binary_relation_exprt{length, ID_le, from_integer(longest, index_type)}});
  }

  for(int k = 0; k <= 9; ++k)
  {
    // The plain notation below 1 starts at 10^-3 (Java) or 10^-4 (Python);
    // JAVA_PLAIN is always plain. The smallest subnormal is above 0.
    const double lower = k > 0                                 ? k
                         : format == float_formatt::JAVA_PLAIN ? 0x1p-1074
                         : python                              ? 1e-4
                                                               : 1e-3;
    constraints.existential.push_back(implies_exprt{
      magnitude_in(lower, k + 1),
      and_exprt{
        equal_exprt{res[offset], from_integer('0' + k, char_type)},
        equal_exprt{res[after_first], point}}});
  }

  // In plain notation, a magnitude of decimal exponent e starts with its
  // leading digit d and has the point after e + 1 digits: the magnitude is
  // at least the nearest value to d * 10^e and below the nearest to
  // (d + 1) * 10^e. (A string of fewer digits that rounds to the value has
  // the same leading digit and exponent.) Checked against Float.toString
  // for every float in [1, 10^7), Double.toString for 20 million doubles
  // and Python's repr for 2 million values up to 10^16. Without it, a
  // prefix of the string of a value in the thousands took a refinement
  // round per value.
  {
    const bool single_type = single;
    const auto nearest = [&](const std::string &text) -> exprt
    {
      return single_type
               ? float_constant(std::strtof(text.c_str(), nullptr), type)
               : float_constant(std::strtod(text.c_str(), nullptr), type);
    };
    const int largest_exponent = python ? 15 : 6;
    for(int e = 1; e <= largest_exponent; ++e)
    {
      for(int d = 1; d <= 9; ++d)
      {
        const exprt lower =
          nearest(std::to_string(d) + "e" + std::to_string(e));
        const exprt upper =
          d < 9 ? nearest(std::to_string(d + 1) + "e" + std::to_string(e))
                : nearest("1e" + std::to_string(e + 1));
        const exprt in_range = or_exprt{
          and_exprt{
            binary_relation_exprt{value, ID_ge, lower},
            binary_relation_exprt{value, ID_lt, upper}},
          and_exprt{
            binary_relation_exprt{value, ID_le, unary_minus_exprt{lower}},
            binary_relation_exprt{value, ID_gt, unary_minus_exprt{upper}}}};
        constraints.existential.push_back(implies_exprt{
          in_range,
          and_exprt{
            equal_exprt{res[offset], from_integer('0' + d, char_type)},
            equal_exprt{
              res[plus_exprt{offset, from_integer(e + 1, index_type)}],
              point}}});
      }
    }
  }

  // The first digit after the point of a magnitude in [1, 10): d where the
  // magnitude is at least the nearest value to k.d and below the nearest
  // to the next tenth. (A string of fewer digits that rounds to the value is
  // on the same side of k.d as the value.) Checked against Float.toString
  // for every float in [1, 10), Double.toString for 30 million doubles and
  // Python's repr for 3 million.
  // Without it, `s.startsWith("1.5")` took a refinement round per value.
  {
    const bool single_type = single;
    const auto nearest = [&](int tenths) -> exprt
    {
      const std::string text =
        std::to_string(tenths / 10) + "." + std::to_string(tenths % 10);
      return single_type
               ? float_constant(std::strtof(text.c_str(), nullptr), type)
               : float_constant(std::strtod(text.c_str(), nullptr), type);
    };
    const plus_exprt after_point{after_first, from_integer(1, index_type)};
    for(int tenths = 10; tenths < 100; ++tenths)
    {
      const exprt lower = nearest(tenths);
      const exprt upper = nearest(tenths + 1);
      const exprt in_range = or_exprt{
        and_exprt{
          binary_relation_exprt{value, ID_ge, lower},
          binary_relation_exprt{value, ID_lt, upper}},
        and_exprt{
          binary_relation_exprt{value, ID_le, unary_minus_exprt{lower}},
          binary_relation_exprt{value, ID_gt, unary_minus_exprt{upper}}}};
      constraints.existential.push_back(implies_exprt{
        in_range,
        equal_exprt{
          res[after_point], from_integer('0' + tenths % 10, char_type)}});
    }
  }
  return constraints;
}

/// String representation of a floating-point value, in \p format (see
/// format_float).
///
/// The previous axioms gave five fractional digits and the integer part
/// modulo 10^5, single-precision digits for doubles in scientific notation,
/// and contradictions for negative values, so they proved wrong results
/// and made paths infeasible. The representation of a value is now known
/// exactly where the value is constant. Otherwise the axioms of
/// float_string_axioms constrain the result, and it is recorded in
/// float_conversions: the refinement compares it with format_float of the
/// value in each model, and excludes the models where they differ.
/// \param f: function application with arguments the length and the
///   characters of the result, and the value
/// \param format: the representation to give
/// \return code 0
std::pair<exprt, string_constraintst>
string_constraint_generatort::add_axioms_for_string_of_float(
  const function_application_exprt &f,
  float_formatt format)
{
  PRECONDITION(f.arguments().size() == 3);
  const array_string_exprt res =
    array_pool.find(f.arguments()[1], f.arguments()[0]);
  const exprt &value = f.arguments()[2];
  const exprt simplified = simplify_expr(value, ns);
  if(simplified.is_constant() && simplified.type().id() == ID_floatbv)
  {
    const ieee_float_valuet constant{to_constant_expr(simplified)};
    const bool single = constant.spec == ieee_float_spect::single_precision();
    if(single || constant.spec == ieee_float_spect::double_precision())
    {
      const double d = single ? constant.to_float() : constant.to_double();
      // the string of each version of Java that may run the program
      const std::vector<std::string> candidates =
        format_float_candidates(d, single, format);
      if(candidates.size() == 1)
        return add_axioms_for_constant(res, candidates.front());
      exprt::operandst disjuncts;
      for(const std::string &text : candidates)
        disjuncts.push_back(string_is(res, text, array_pool));
      string_constraintst constraints;
      constraints.existential.push_back(disjunction(disjuncts));
      return {from_integer(0, get_return_code_type()), constraints};
    }
  }
  float_conversions.push_back({value, res, format});
  if(value.type().id() != ID_floatbv)
    return {from_integer(0, get_return_code_type()), {}};
  return {
    from_integer(0, get_return_code_type()),
    float_string_axioms(res, value, format)};
}

string_constraintst
string_constraint_generatort::add_axioms_for_java_format_float(
  const array_string_exprt &res,
  const exprt &value,
  char conversion,
  int precision)
{
  PRECONDITION(value.type() == ieee_float_spect::double_precision().to_type());
  const exprt simplified = simplify_expr(value, ns);
  if(simplified.is_constant())
  {
    // the string of each version of Java that may run the program
    const ieee_float_valuet constant{to_constant_expr(simplified)};
    std::vector<std::string> candidates;
    for(const java_digitst digits : java_formatter_digits())
    {
      std::string text =
        java_format_float(constant.to_double(), conversion, precision, digits);
      if(
        std::find(candidates.begin(), candidates.end(), text) ==
        candidates.end())
        candidates.push_back(std::move(text));
    }
    if(candidates.size() == 1)
      return add_axioms_for_constant(res, candidates.front()).second;
    exprt::operandst disjuncts;
    for(const std::string &text : candidates)
      disjuncts.push_back(string_is(res, text, array_pool));
    string_constraintst constraints;
    constraints.existential.push_back(disjunction(disjuncts));
    return constraints;
  }
  float_conversiont conversion_record{value, res, float_formatt::JAVA};
  conversion_record.java_conversion = conversion;
  conversion_record.java_precision = precision;
  float_conversions.push_back(conversion_record);
  // the lengths that the strings have, which also keeps the strings of the
  // models short
  const exprt length = array_pool.get_or_create_length(res);
  exprt::operandst lengths;
  for(const auto &interval : java_format_float_lengths(conversion, precision))
  {
    lengths.push_back(and_exprt{
      binary_relation_exprt{
        length, ID_ge, from_integer(interval.first, length.type())},
      binary_relation_exprt{
        length, ID_le, from_integer(interval.second, length.type())}});
  }
  string_constraintst constraints;
  constraints.existential.push_back(disjunction(lengths));
  // The characters are ASCII.
  const symbol_exprt qvar =
    fresh_symbol("QA_format_float_ascii", length.type());
  constraints.universal.emplace_back(
    qvar,
    zero_if_negative(length),
    binary_relation_exprt{
      res[qvar],
      ID_lt,
      from_integer(128, to_type_with_subtype(res.content().type()).subtype())},
    ns,
    message_handler);
  return constraints;
}
