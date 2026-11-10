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

#include <cmath>
#include <optional>

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
  const auto closest = [&](int s) -> std::optional<mp_integer> {
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
    const auto distance = [&](const mp_integer &n, int t) {
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
  return digits.substr(0, integer_digits) + '.' +
         digits.substr(integer_digits);
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
