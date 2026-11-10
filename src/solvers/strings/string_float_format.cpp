/*******************************************************************\

Module: String representations of floating-point values

Author: Diffblue Ltd.

\*******************************************************************/

/// \file
/// String representations of floating-point values, as Java's
/// Float.toString and Double.toString and Python's repr give them.

#include "string_float_format.h"

#include <util/arith_tools.h>
#include <util/invariant.h>
#include <util/mp_arith.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <vector>

/// \p value * 2^\p binary * 10^\p decimal, compared with \p other *
/// 2^\p other_binary: the sign of the difference.
static int compare_scaled(
  const mp_integer &value,
  int binary,
  int decimal,
  const mp_integer &other,
  int other_binary)
{
  // Bring both sides to integers by moving negative exponents across.
  mp_integer lhs = value;
  mp_integer rhs = other;
  const int b = binary - other_binary;
  if(b >= 0)
    lhs *= power(2, b);
  else
    rhs *= power(2, -b);
  if(decimal >= 0)
    lhs *= power(10, decimal);
  else
    rhs *= power(10, -decimal);
  return lhs < rhs ? -1 : lhs > rhs ? 1 : 0;
}

/// The smallest n such that n * 10^\p decimal >= \p bound * 2^\p binary
/// (strictly greater if \p strict).
static mp_integer
ceil_multiple(const mp_integer &bound, int binary, int decimal, bool strict)
{
  // bound * 2^binary / 10^decimal as numerator / denominator.
  mp_integer numerator = bound;
  mp_integer denominator = 1;
  if(binary >= 0)
    numerator *= power(2, binary);
  else
    denominator *= power(2, -binary);
  if(decimal >= 0)
    denominator *= power(10, decimal);
  else
    numerator *= power(10, -decimal);
  mp_integer n = numerator / denominator;
  const bool exact = n * denominator == numerator;
  if(!exact || strict)
    n += 1;
  return n;
}

/// The shortest decimal in the rounding interval of \p value, as Java's
/// Float.toString and Double.toString (since Java 19) and Python's repr
/// choose it: the fewest significant digits, then the closest to
/// \p value, then an even last digit. Java takes two digits where one
/// would do (\p at_least_two).
/// \param value: a finite positive value
/// \param single_precision: whether \p value is a float
/// \param at_least_two: whether to give at least two significant digits
/// \param [out] exponent: the decimal exponent of the first digit
/// \return the digits, without trailing zeros
static std::string shortest_digits(
  double value,
  bool single_precision,
  bool at_least_two,
  int &exponent)
{
  PRECONDITION(std::isfinite(value) && value > 0);
  // value = m * 2^e with m of p bits, or fewer for subnormals.
  const int p = single_precision ? 24 : 53;
  const int e_min = single_precision ? -149 : -1074;
  int x;
  const double fraction = std::frexp(value, &x);
  int e = x - p;
  mp_integer m = static_cast<long long>(std::ldexp(fraction, p));
  if(e < e_min)
  {
    m /= power(2, e_min - e);
    e = e_min;
  }

  // The rounding interval, in units of 2^(e-2): its half-gaps to the
  // neighbours, the lower one half as wide at a power of two.
  const bool boundary = m == power(2, p - 1) && e > e_min;
  const mp_integer centre = 4 * m;
  const mp_integer low = centre - (boundary ? 1 : 2);
  const mp_integer high = centre + 2;
  // Ties round to even, so the bounds belong to the interval for an even m.
  const bool inclusive = m % 2 == 0;
  const int unit = e - 2;

  // The multiple of 10^s in the interval closest to the value, if any.
  const auto closest = [&](int s) -> std::optional<mp_integer>
  {
    const mp_integer first = ceil_multiple(low, unit, s, !inclusive);
    const int to_high = compare_scaled(first, 0, s, high, unit);
    if(to_high > 0 || (to_high == 0 && !inclusive))
      return {};
    const mp_integer last = ceil_multiple(high, unit, s, inclusive) - 1;
    // The smallest multiple at or above the value, or the one below it.
    mp_integer nearest = ceil_multiple(centre, unit, s, false);
    const mp_integer below = nearest - 1;
    // Distances: value - below * 10^s and nearest * 10^s - value, compared
    // as (below + nearest) * 10^s with 2 * value.
    const int c = compare_scaled(below + nearest, 0, s, 2 * centre, unit);
    if(c > 0 || (c == 0 && below % 2 == 0))
      nearest = below;
    if(nearest < first)
      nearest = first;
    if(nearest > last)
      nearest = last;
    return nearest;
  };

  // The largest step 10^s with a multiple in the interval gives the fewest
  // digits.
  int s = static_cast<int>(std::floor(std::log10(value))) + 2;
  std::optional<mp_integer> nearest;
  for(; !(nearest = closest(s)).has_value(); --s)
  {
  }
  std::string digits = integer2string(*nearest);
  int trailing = 0;
  while(digits.size() > 1 && digits.back() == '0')
  {
    digits.pop_back();
    ++trailing;
  }
  if(at_least_two && digits.size() == 1)
  {
    // The closest decimal of two significant digits: a multiple of
    // 10^(d-1) for a leading digit at 10^d (from 10 * 10^(d-1)), or of
    // 10^(d-2) for one at 10^(d-1) (up to 99 * 10^(d-2)).
    const int d = s + trailing;
    // |n * 10^t - value|, scaled by 10^max(0, 2 - d) * 2^max(0, -unit)
    // to an integer.
    const int ten_scale = d - 2 < 0 ? 2 - d : 0;
    const int two_scale = unit < 0 ? -unit : 0;
    const mp_integer scaled_value =
      centre * power(2, unit + two_scale) * power(10, ten_scale);
    const auto distance = [&](const mp_integer &n, int t)
    {
      const mp_integer diff =
        n * power(10, t + ten_scale) * power(2, two_scale) - scaled_value;
      return diff < 0 ? -diff : diff;
    };
    std::optional<mp_integer> upper = closest(d - 1);
    std::optional<mp_integer> lower = closest(d - 2);
    if(lower.has_value() && *lower > 99)
      lower.reset();
    CHECK_RETURN(upper.has_value() || lower.has_value());
    bool take_lower = !upper.has_value();
    if(upper.has_value() && lower.has_value())
    {
      const mp_integer du = distance(*upper, d - 1);
      const mp_integer dl = distance(*lower, d - 2);
      take_lower = dl < du || (dl == du && *lower % 2 == 0);
    }
    s = take_lower ? d - 2 : d - 1;
    digits = integer2string(take_lower ? *lower : *upper);
    trailing = 0;
    while(digits.size() > 1 && digits.back() == '0')
    {
      digits.pop_back();
      ++trailing;
    }
  }
  exponent = s + trailing + static_cast<int>(digits.size()) - 1;
  return digits;
}

/// \p digits with the decimal point after the digit of exponent 0, and at
/// least one digit after it.
static std::string plain(const std::string &digits, int exponent)
{
  std::string result;
  if(exponent < 0)
  {
    result = "0.";
    result.append(static_cast<std::size_t>(-exponent - 1), '0');
    result += digits;
    return result;
  }
  const std::size_t integer_digits = static_cast<std::size_t>(exponent) + 1;
  if(digits.size() <= integer_digits)
  {
    result = digits;
    result.append(integer_digits - digits.size(), '0');
    return result + ".0";
  }
  return digits.substr(0, integer_digits) + '.' + digits.substr(integer_digits);
}

static std::string java_scientific(const std::string &digits, int exponent)
{
  const std::string fraction = digits.size() > 1 ? digits.substr(1) : "0";
  return digits.substr(0, 1) + '.' + fraction + 'E' + std::to_string(exponent);
}

static std::string python_scientific(const std::string &digits, int exponent)
{
  std::string result = digits.substr(0, 1);
  if(digits.size() > 1)
    result += '.' + digits.substr(1);
  result += exponent < 0 ? "e-" : "e+";
  const int magnitude = exponent < 0 ? -exponent : exponent;
  if(magnitude < 10)
    result += '0';
  return result + std::to_string(magnitude);
}

std::string
format_float(double value, bool single_precision, float_formatt format)
{
  const bool negative = std::signbit(value);
  const double magnitude = std::fabs(value);
  const bool python = format == float_formatt::PYTHON;
  const bool signed_format =
    format == float_formatt::JAVA || format == float_formatt::PYTHON;
  const std::string sign = signed_format && negative ? "-" : "";

  if(std::isnan(value))
    return python ? "nan" : "NaN";
  if(std::isinf(value))
    return sign + (python ? "inf" : "Infinity");
  if(magnitude == 0)
    return sign + "0.0";

  int exponent;
  const std::string digits =
    shortest_digits(magnitude, single_precision, !python, exponent);
  switch(format)
  {
  case float_formatt::JAVA_PLAIN:
    return plain(digits, exponent);
  case float_formatt::JAVA_SCIENTIFIC:
    return java_scientific(digits, exponent);
  case float_formatt::JAVA:
    return sign + (magnitude >= 1e-3 && magnitude < 1e7
                     ? plain(digits, exponent)
                     : java_scientific(digits, exponent));
  case float_formatt::PYTHON:
    return sign + (exponent >= -4 && exponent < 16
                     ? plain(digits, exponent)
                     : python_scientific(digits, exponent));
  }
  UNREACHABLE;
}

std::pair<std::size_t, std::size_t>
float_string_length_bounds(bool single_precision, float_formatt format)
{
  // At most this many significant digits; decimal exponents of the first
  // digit from -min_exponent to max_exponent; and the longest exponent with
  // its sign.
  const std::size_t digits = single_precision ? 9 : 17;
  const std::size_t min_exponent = single_precision ? 45 : 324;
  const std::size_t max_exponent = single_precision ? 38 : 308;
  const std::size_t exponent_length = single_precision ? 3 : 4;
  // "0.0", "1.0", "NaN" and "inf" are the shortest.
  const std::size_t shortest = 3;
  switch(format)
  {
  case float_formatt::JAVA_PLAIN:
    // "0." and the zeros before the digits of the smallest value, or the
    // integer digits of the largest and ".0".
    return {
      shortest,
      std::max(2 + (min_exponent - 1) + digits, max_exponent + 1 + 2)};
  case float_formatt::JAVA_SCIENTIFIC:
    // "d.", the other digits (at least one), "E" and the exponent.
    return {shortest, 2 + (digits - 1) + 1 + exponent_length};
  case float_formatt::JAVA:
    // A sign, and plain notation from 10^-3 ("0.00" and the digits) up to
    // 10^7 (7 integer digits and the others), or scientific notation.
    return {
      shortest,
      1 + std::max(
            {4 + digits,
             7 + 1 + (digits - 7),
             2 + (digits - 1) + 1 + exponent_length})};
  case float_formatt::PYTHON:
    // A sign, and plain notation from 10^-4 ("0.000" and the digits) up to
    // 10^16 (16 integer digits and ".0", or the digits and a point), or
    // scientific notation ("d.", the other digits, "e", the sign and at
    // least two exponent digits).
    return {
      shortest,
      1 + std::max(
            {5 + digits,
             std::size_t{18},
             digits + 1,
             2 + (digits - 1) + 2 + (exponent_length - 1)})};
  }
  UNREACHABLE;
}

std::optional<double> parse_float_string(
  const std::string &text,
  bool single_precision,
  float_formatt format)
{
  // The digits, points, exponents, signs and special words of the formats,
  // all of which strtod reads.
  if(text.empty() || text.size() > 400)
    return {};
  for(const char c : text)
  {
    if(
      !std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '-' &&
      c != '+')
    {
      return {};
    }
  }
  // Read in the precision itself: rounding to a double first could round
  // differently.
  char *end = nullptr;
  const double value = single_precision
                         ? static_cast<double>(std::strtof(text.c_str(), &end))
                         : std::strtod(text.c_str(), &end);
  if(end != text.c_str() + text.size())
    return {};
  if(format_float(value, single_precision, format) != text)
    return {};
  return value;
}

namespace
{
/// A part of a pattern: a character of \p characters, repeated from
/// \p min_count times up to \p max_count times (0 for no limit).
struct float_pattern_partt
{
  std::string characters;
  std::size_t min_count;
  std::size_t max_count;
};

using float_patternt = std::vector<float_pattern_partt>;
} // namespace

/// Whether \p text from \p position is a prefix of a match of the parts
/// of \p pattern from \p part on.
static bool viable(
  const std::string &text,
  std::size_t position,
  const float_patternt &pattern,
  std::size_t part)
{
  if(position == text.size())
    return true;
  if(part == pattern.size())
    return false;
  const float_pattern_partt &p = pattern[part];
  for(std::size_t count = 0;; ++count)
  {
    const std::size_t i = position + count;
    if(count >= p.min_count && viable(text, i, pattern, part + 1))
      return true;
    if(i == text.size())
      return true;
    if(p.max_count != 0 && count == p.max_count)
      return false;
    if(p.characters.find(text[i]) == std::string::npos)
      return false;
  }
}

std::size_t
float_string_viable_prefix(const std::string &text, float_formatt format)
{
  const std::string digits = "0123456789";
  const std::string non_zero = "123456789";
  const bool python = format == float_formatt::PYTHON;
  const bool signed_format =
    format == float_formatt::JAVA || format == float_formatt::PYTHON;
  const float_pattern_partt sign{"-", 0, 1};

  const auto literal = [&](const std::string &word, bool with_sign)
  {
    float_patternt pattern;
    if(with_sign && signed_format)
      pattern.push_back(sign);
    for(const char c : word)
      pattern.push_back({std::string(1, c), 1, 1});
    return pattern;
  };
  const auto with_sign = [&](float_patternt pattern)
  {
    if(signed_format)
      pattern.insert(pattern.begin(), sign);
    return pattern;
  };

  std::vector<float_patternt> patterns;
  if(format != float_formatt::JAVA_SCIENTIFIC)
    patterns.push_back(
      with_sign({{digits, 1, 0}, {".", 1, 1}, {digits, 1, 0}}));
  if(format != float_formatt::JAVA_PLAIN)
  {
    if(python)
    {
      patterns.push_back(with_sign(
        {{non_zero, 1, 1},
         {".", 0, 1},
         {digits, 0, 0},
         {"e", 1, 1},
         {"+-", 1, 1},
         {digits, 2, 0}}));
    }
    else
    {
      patterns.push_back(with_sign(
        {{non_zero, 1, 1},
         {".", 1, 1},
         {digits, 1, 0},
         {"E", 1, 1},
         {"-", 0, 1},
         {digits, 1, 0}}));
    }
  }
  patterns.push_back(literal(python ? "nan" : "NaN", false));
  patterns.push_back(literal(python ? "inf" : "Infinity", true));
  if(format == float_formatt::JAVA_SCIENTIFIC)
    patterns.push_back(literal("0.0", false));

  std::size_t longest = 0;
  for(std::size_t n = 1; n <= text.size(); ++n)
  {
    const std::string prefix = text.substr(0, n);
    const bool any = std::any_of(
      patterns.begin(),
      patterns.end(),
      [&](const float_patternt &pattern)
      { return viable(prefix, 0, pattern, 0); });
    if(!any)
      break;
    longest = n;
  }
  return longest;
}
