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
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
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

/// The decimal f * 10^e of n digits (n = 0 for zero) of the fields of
/// FormattedFPDecimal of Java 21, whose operations these follow.
struct formatted_decimalt
{
  unsigned long long f = 0;
  int e = 0;
  int n = 0;

  /// Round to \p pp digits, half up.
  void round(long long pp)
  {
    if(n == 0 || pp < 0)
    {
      f = 0;
      e = 0;
      n = 1;
      return;
    }
    if(pp >= n)
      return;
    const int p = static_cast<int>(pp);
    e += n - p;
    unsigned long long pow10 = 1;
    for(int i = 0; i < n - p; ++i)
      pow10 *= 10;
    f = (f + pow10 / 2) / pow10;
    if(p == 0)
    {
      n = 1;
      if(f == 0)
        e = 0;
      return;
    }
    n = p;
    unsigned long long pow10_p = 1;
    for(int i = 0; i < p; ++i)
      pow10_p *= 10;
    if(f == pow10_p)
    {
      f /= 10;
      e += 1;
    }
  }

  int exponent_rounded() const
  {
    return n + e - 1;
  }

  /// The n digits of f.
  std::string digits() const
  {
    std::string result = std::to_string(f);
    if(static_cast<int>(result.size()) < n)
      result.insert(0, static_cast<std::size_t>(n) - result.size(), '0');
    return result;
  }

  std::string plain_chars() const
  {
    const std::string d = digits();
    if(e >= 0)
      return d + std::string(static_cast<std::size_t>(e), '0');
    if(n + e > 0)
    {
      const auto point = static_cast<std::size_t>(n + e);
      return d.substr(0, point) + '.' + d.substr(point);
    }
    return "0." + std::string(static_cast<std::size_t>(-e - n), '0') + d;
  }

  std::string scientific_chars(int prec) const
  {
    const std::string d = digits();
    if(prec == 0)
      return d.substr(0, 1);
    return d.substr(0, 1) + '.' + d.substr(1);
  }

  /// The sign and at least two digits of the exponent.
  std::string exponent_chars() const
  {
    const int er = exponent_rounded();
    std::string magnitude = std::to_string(er < 0 ? -er : er);
    if(magnitude.size() < 2)
      magnitude.insert(0, 1, '0');
    return (er >= 0 ? "+" : "-") + magnitude;
  }
};

/// Formatter.addZeros: the digits after the point of \p mantissa, as many
/// as \p prec.
static void add_zeros(std::string &mantissa, int prec)
{
  const std::size_t point = mantissa.find('.');
  const bool need_dot = point == std::string::npos;
  const int out_prec =
    need_dot ? 0 : static_cast<int>(mantissa.size() - point - 1);
  if(out_prec >= prec)
    return;
  if(need_dot)
    mantissa += '.';
  mantissa.append(static_cast<std::size_t>(prec - out_prec), '0');
}

/// Double.toHexString of a finite \p d >= 0, without its "0x".
static std::string hex_string(double d)
{
  if(d == 0)
    return "0.0p0";
  std::uint64_t bits;
  std::memcpy(&bits, &d, sizeof(bits));
  const bool subnormal = d < std::numeric_limits<double>::min();
  const std::uint64_t significand = bits & 0x000FFFFFFFFFFFFFULL;
  std::string hex;
  for(int shift = 48; shift >= 0; shift -= 4)
    hex += "0123456789abcdef"[(significand >> shift) & 0xF];
  const std::size_t last = hex.find_last_not_of('0');
  hex = last == std::string::npos ? "0" : hex.substr(0, last + 1);
  const int exponent = subnormal ? -1022 : std::ilogb(d);
  return std::string{subnormal ? "0." : "1."} + hex + 'p' +
         std::to_string(exponent);
}

/// Formatter.hexDouble of a finite \p d >= 0: the digits of %a with the
/// precision \p prec (0 for all), without "0x".
static std::string hex_double(double d, int prec)
{
  if(d == 0 || prec == 0 || prec >= 13)
    return hex_string(d);
  const bool subnormal = d < std::numeric_limits<double>::min();
  if(subnormal)
    d *= std::ldexp(1.0, 54);
  const int shift = 52 - 4 * prec;
  std::uint64_t bits;
  std::memcpy(&bits, &d, sizeof(bits));
  std::uint64_t significand = (bits & 0x7FFFFFFFFFFFFFFFULL) >> shift;
  const std::uint64_t rounding_bits = bits & ~(~0ULL << shift);
  const bool least_zero = (significand & 1) == 0;
  const bool round = ((1ULL << (shift - 1)) & rounding_bits) != 0;
  const bool sticky =
    shift > 1 && (~(1ULL << (shift - 1)) & rounding_bits) != 0;
  if((least_zero && round && sticky) || (!least_zero && round))
    ++significand;
  const std::uint64_t rounded_bits =
    (bits & 0x8000000000000000ULL) | (significand << shift);
  double result;
  std::memcpy(&result, &rounded_bits, sizeof(result));
  if(std::isinf(result))
    return "1.0p1024";
  const std::string text = hex_string(result);
  if(!subnormal)
    return text;
  const std::size_t p = text.find('p');
  return text.substr(0, p) + 'p' +
         std::to_string(std::stoi(text.substr(p + 1)) - 54);
}

std::string java_format_float(double value, char conversion, int precision)
{
  if(std::isnan(value))
    return "NaN";
  const std::string sign = std::signbit(value) ? "-" : "";
  const double v = std::fabs(value);
  if(std::isinf(v))
    return sign + "Infinity";

  if(conversion == 'a')
  {
    const int prec = precision == -1 ? 0 : precision == 0 ? 1 : precision;
    const std::string s = hex_double(v, prec);
    const std::size_t p = s.find('p');
    std::string mantissa = s.substr(0, p);
    if(prec != 0)
      add_zeros(mantissa, prec);
    return sign + "0x" + mantissa + s.substr(p);
  }

  // DoubleToDecimal.split: the shortest decimal (see shortest_digits)
  formatted_decimalt fd;
  if(v != 0)
  {
    int exponent;
    const std::string digits = shortest_digits(v, false, true, exponent);
    fd.f = std::stoull(digits);
    fd.n = static_cast<int>(digits.size());
    fd.e = exponent - fd.n + 1;
  }
  if(conversion == 'e')
  {
    const int prec = precision == -1 ? 6 : precision;
    fd.round(prec + 1LL);
    std::string mantissa = fd.scientific_chars(prec);
    add_zeros(mantissa, prec);
    return sign + mantissa + 'e' + (v == 0 ? "+00" : fd.exponent_chars());
  }
  if(conversion == 'f')
  {
    const int prec = precision == -1 ? 6 : precision;
    fd.round(static_cast<long long>(fd.n) + fd.e + prec);
    std::string mantissa = fd.plain_chars();
    add_zeros(mantissa, prec);
    return sign + mantissa;
  }
  PRECONDITION(conversion == 'g');
  int prec = precision == -1 ? 6 : precision == 0 ? 1 : precision;
  std::string mantissa;
  std::optional<std::string> exponent;
  if(v == 0)
  {
    mantissa = "0";
    prec -= 1;
  }
  else
  {
    fd.round(prec);
    const int er = fd.exponent_rounded();
    if(-4 <= er && er < prec)
    {
      mantissa = fd.plain_chars();
      prec -= er + 1;
    }
    else
    {
      mantissa = fd.scientific_chars(prec - 1);
      exponent = fd.exponent_chars();
      prec -= 1;
    }
  }
  add_zeros(mantissa, prec);
  return sign + mantissa + (exponent.has_value() ? 'e' + *exponent : "");
}

std::size_t java_format_float_max_length(char conversion, int precision)
{
  // a sign, and the digits of the largest exponent or integer part
  const std::size_t prec =
    precision < 0 ? 6 : static_cast<std::size_t>(precision);
  switch(conversion)
  {
  case 'f':
    return 1 + 309 + 1 + prec;
  case 'e':
  case 'g':
    return 1 + 2 + std::max<std::size_t>(prec, 1) + 6;
  default:
    PRECONDITION(conversion == 'a');
    return 1 + 4 + std::max<std::size_t>(prec, 13) + 6;
  }
}

std::vector<std::pair<std::size_t, std::size_t>>
java_format_float_lengths(char conversion, int precision)
{
  // the lengths of the magnitudes of the finite values
  std::vector<std::size_t> magnitudes;
  // the largest decimal exponent of the first digit of a double
  const int max_exponent = 308;
  // an exponent of 2 or 3 digits and its sign and mark
  const std::size_t exponents[] = {4, 5};
  if(conversion == 'f')
  {
    const std::size_t fraction =
      precision == 0 ? 0 : 1 + (precision < 0 ? 6 : precision);
    // 1 to 309 digits before the point
    for(std::size_t digits = 1; digits <= 309; ++digits)
      magnitudes.push_back(digits + fraction);
  }
  else if(conversion == 'e')
  {
    const std::size_t prec = precision < 0 ? 6 : precision;
    const std::size_t mantissa = prec == 0 ? 1 : 2 + prec;
    for(const std::size_t e : exponents)
      magnitudes.push_back(mantissa + e);
  }
  else if(conversion == 'g')
  {
    const int prec = precision < 0 ? 6 : precision == 0 ? 1 : precision;
    // zero
    magnitudes.push_back(prec == 1 ? 1 : 1 + prec);
    // plain for the exponents in [-4, prec), scientific otherwise
    for(int er = -4; er < prec && er <= max_exponent; ++er)
    {
      const int fraction = prec - er - 1;
      magnitudes.push_back(
        er >= 0
          ? static_cast<std::size_t>(er + 1 + (fraction > 0 ? 1 + fraction : 0))
          : static_cast<std::size_t>(prec - er + 1));
    }
    const std::size_t mantissa = prec == 1 ? 1 : 1 + prec;
    // exponents of 2 digits (from -99 to -5) and of 3 digits (from -324 to
    // -100)
    for(const std::size_t e : exponents)
      magnitudes.push_back(mantissa + e);
  }
  else
  {
    PRECONDITION(conversion == 'a');
    // "0x", a digit and a point, the hexadecimal digits, 'p' and an
    // exponent from -1074 to 1024 (of 1 to 5 characters)
    std::size_t min_digits = 1;
    std::size_t max_digits = 13;
    if(precision > 0)
      min_digits = max_digits = static_cast<std::size_t>(precision);
    for(std::size_t digits = min_digits; digits <= max_digits; ++digits)
    {
      for(std::size_t e = 1; e <= 5; ++e)
        magnitudes.push_back(5 + digits + e);
    }
  }
  // NaN, Infinity and -Infinity, and the magnitudes with and without a
  // sign
  std::vector<std::size_t> lengths{3, 8, 9};
  for(const std::size_t m : magnitudes)
  {
    lengths.push_back(m);
    lengths.push_back(m + 1);
  }
  std::sort(lengths.begin(), lengths.end());
  std::vector<std::pair<std::size_t, std::size_t>> intervals;
  for(const std::size_t l : lengths)
  {
    if(!intervals.empty() && l <= intervals.back().second + 1)
      intervals.back().second = std::max(intervals.back().second, l);
    else
      intervals.emplace_back(l, l);
  }
  return intervals;
}

/// The exact value of a magnitude that java_format_float prints with the
/// conversion \p conversion: m * 10^exponent (m * 2^exponent for 'a'), or
/// infinity.
struct printed_magnitudet
{
  bool infinite = false;
  mp_integer m = 0;
  int exponent = 0;
};

/// The value of \p text, a magnitude printed by java_format_float with the
/// conversion \p conversion, if it has the shape of one.
static std::optional<printed_magnitudet>
parse_printed_magnitude(const std::string &text, char conversion)
{
  printed_magnitudet result;
  if(text == "Infinity")
  {
    result.infinite = true;
    return result;
  }
  std::size_t i = 0;
  const bool hex = conversion == 'a';
  const unsigned radix = hex ? 16 : 10;
  if(hex)
  {
    if(text.compare(0, 2, "0x") != 0)
      return {};
    i = 2;
  }
  const auto digit = [&](char c) -> int
  {
    if(c >= '0' && c <= '9')
      return c - '0';
    if(hex && c >= 'a' && c <= 'f')
      return c - 'a' + 10;
    return -1;
  };
  std::size_t count = 0;
  int fraction_digits = 0;
  bool point = false;
  for(; i < text.size(); ++i)
  {
    if(text[i] == '.' && !point)
    {
      point = true;
      continue;
    }
    const int d = digit(text[i]);
    if(d < 0)
      break;
    result.m = result.m * radix + d;
    ++count;
    if(point)
      ++fraction_digits;
  }
  if(count == 0 || count > 400)
    return {};
  // the scale of the fraction digits
  result.exponent = hex ? -4 * fraction_digits : -fraction_digits;
  const char mark = hex ? 'p' : 'e';
  if(i < text.size())
  {
    if(text[i] != mark || i + 1 >= text.size())
      return {};
    ++i;
    bool negative = false;
    if(text[i] == '-' || text[i] == '+')
    {
      negative = text[i] == '-';
      ++i;
    }
    if(i >= text.size() || text.size() - i > 5)
      return {};
    int exponent = 0;
    for(; i < text.size(); ++i)
    {
      if(text[i] < '0' || text[i] > '9')
        return {};
      exponent = exponent * 10 + (text[i] - '0');
    }
    result.exponent += negative ? -exponent : exponent;
  }
  return result;
}

/// The sign of \p a - \p b, for magnitudes of the same conversion, whose
/// exponents are of the base \p base.
static int compare_printed(
  const printed_magnitudet &a,
  const printed_magnitudet &b,
  unsigned base)
{
  if(a.infinite || b.infinite)
    return a.infinite == b.infinite ? 0 : a.infinite ? 1 : -1;
  mp_integer x = a.m;
  mp_integer y = b.m;
  if(a.exponent > b.exponent)
    x *= power(base, a.exponent - b.exponent);
  else
    y *= power(base, b.exponent - a.exponent);
  return x < y ? -1 : x > y ? 1 : 0;
}

java_format_float_preimaget java_format_float_preimage(
  const std::string &text,
  char conversion,
  int precision)
{
  java_format_float_preimaget result;
  if(text == "NaN")
  {
    result.nan = true;
    return result;
  }
  result.negative = !text.empty() && text[0] == '-';
  const std::string magnitude_text = text.substr(result.negative ? 1 : 0);
  const auto target = parse_printed_magnitude(magnitude_text, conversion);
  if(!target.has_value())
    return result;
  const unsigned base = conversion == 'a' ? 2 : 10;
  // the magnitude printed for the non-negative double of bits \p bits
  const auto printed = [&](std::uint64_t bits)
  {
    double d;
    std::memcpy(&d, &bits, sizeof(d));
    const auto value = parse_printed_magnitude(
      java_format_float(d, conversion, precision), conversion);
    INVARIANT(value.has_value(), "java_format_float prints a magnitude");
    return *value;
  };
  // The printed magnitude does not decrease with the value, and the
  // non-negative doubles are ordered as their bits, up to infinity.
  const std::uint64_t infinity = 0x7FF0000000000000ULL;
  // the first bits whose magnitude is at least the target
  std::uint64_t low = 0;
  std::uint64_t high = infinity + 1;
  while(low < high)
  {
    const std::uint64_t middle = low + (high - low) / 2;
    if(compare_printed(printed(middle), *target, base) >= 0)
      high = middle;
    else
      low = middle + 1;
  }
  const std::uint64_t first = low;
  // the first bits whose magnitude is beyond the target
  high = infinity + 1;
  while(low < high)
  {
    const std::uint64_t middle = low + (high - low) / 2;
    if(compare_printed(printed(middle), *target, base) > 0)
      high = middle;
    else
      low = middle + 1;
  }
  if(first >= low)
    return result;
  double d;
  std::memcpy(&d, &first, sizeof(d));
  // the target as it is printed, as text may differ (as "1.50" from "1.5")
  if(java_format_float(d, conversion, precision) != magnitude_text)
    return result;
  result.empty = false;
  result.low = d;
  const std::uint64_t last = low - 1;
  std::memcpy(&result.high, &last, sizeof(result.high));
  return result;
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
