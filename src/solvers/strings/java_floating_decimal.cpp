/*******************************************************************\

Module: The strings of floating-point values of Java 18 and earlier

Author: Diffblue Ltd.

\*******************************************************************/

/// \file
/// A port of BinaryToASCIIBuffer.dtoa of jdk.internal.math.FloatingDecimal
/// (unchanged from Java 8 to Java 20), of FormattedFloatingDecimal and of
/// the printing of doubles of java.util.Formatter of Java 20 and earlier.
/// The int and long arithmetic of Java wraps, as here.

#include "java_floating_decimal.h"

#include <util/arith_tools.h>
#include <util/invariant.h>
#include <util/mp_arith.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace
{
// Java's int and long operations, which wrap
int32_t imul(int32_t a, int32_t b)
{
  return static_cast<int32_t>(
    static_cast<uint32_t>(a) * static_cast<uint32_t>(b));
}
int32_t iadd(int32_t a, int32_t b)
{
  return static_cast<int32_t>(
    static_cast<uint32_t>(a) + static_cast<uint32_t>(b));
}
int32_t isub(int32_t a, int32_t b)
{
  return static_cast<int32_t>(
    static_cast<uint32_t>(a) - static_cast<uint32_t>(b));
}
int32_t ishl(int32_t a, int s)
{
  return static_cast<int32_t>(static_cast<uint32_t>(a) << (s & 31));
}
int64_t lmul(int64_t a, int64_t b)
{
  return static_cast<int64_t>(
    static_cast<uint64_t>(a) * static_cast<uint64_t>(b));
}
int64_t ladd(int64_t a, int64_t b)
{
  return static_cast<int64_t>(
    static_cast<uint64_t>(a) + static_cast<uint64_t>(b));
}
int64_t lsub(int64_t a, int64_t b)
{
  return static_cast<int64_t>(
    static_cast<uint64_t>(a) - static_cast<uint64_t>(b));
}
int64_t lshl(int64_t a, int s)
{
  return static_cast<int64_t>(static_cast<uint64_t>(a) << (s & 63));
}

const int exp_shift = 52;
const int64_t fract_hob = int64_t{1} << exp_shift;
const int64_t exp_one = int64_t{1023} << exp_shift;
const int64_t signif_bit_mask = 0x000FFFFFFFFFFFFFLL;
const int64_t exp_bit_mask = 0x7FF0000000000000LL;
const int64_t sign_bit_mask = static_cast<int64_t>(0x8000000000000000ULL);
const int max_small_bin_exp = 62;
const int min_small_bin_exp = -(63 / 3);

// approximately ceil(log2(5^i))
const int n_5_bits[] = {0,  3,  5,  7,  10, 12, 14, 17, 19, 21, 24, 26, 28, 31,
                        33, 35, 38, 40, 42, 45, 47, 49, 52, 54, 56, 59, 61};
const int n_5_bits_length = 27;
// FDBigInteger.SMALL_5_POW has 5^0 to 5^13, LONG_5_POW 5^0 to 5^26.
const int long_5_pow_length = 27;

int32_t small_5_pow(int i)
{
  int32_t r = 1;
  for(int k = 0; k < i; ++k)
    r *= 5;
  return r;
}

int64_t long_5_pow(int i)
{
  int64_t r = 1;
  for(int k = 0; k < i; ++k)
    r *= 5;
  return r;
}

const int insignificant_digits_number[] = {
  0,  0,  0,  0,  1,  1,  1,  2,  2,  2,  3,  3,  3,  3,  4,  4,
  4,  5,  5,  5,  6,  6,  6,  6,  7,  7,  7,  8,  8,  8,  9,  9,
  9,  9,  10, 10, 10, 11, 11, 11, 12, 12, 12, 12, 13, 13, 13, 14,
  14, 14, 15, 15, 15, 15, 16, 16, 16, 17, 17, 17, 18, 18, 18, 19};

int insignificant_digits_for_pow2(int p2)
{
  if(p2 > 1 && p2 < 64)
    return insignificant_digits_number[p2];
  return 0;
}

int number_of_trailing_zeros(int64_t x)
{
  if(x == 0)
    return 64;
  int n = 0;
  while(((static_cast<uint64_t>(x) >> n) & 1) == 0)
    ++n;
  return n;
}

int number_of_leading_zeros(uint64_t x)
{
  if(x == 0)
    return 64;
  int n = 0;
  while((x & (uint64_t{1} << 63)) == 0)
  {
    x <<= 1;
    ++n;
  }
  return n;
}

double long_bits_to_double(int64_t bits)
{
  double d;
  std::memcpy(&d, &bits, sizeof(d));
  return d;
}

int64_t double_to_bits(double d)
{
  int64_t bits;
  std::memcpy(&bits, &d, sizeof(bits));
  return bits;
}

/// FloatingDecimal.estimateDecExp (each operation rounded, as in Java)
int estimate_dec_exp(int64_t fract_bits, int bin_exp)
{
  volatile double d2 =
    long_bits_to_double(exp_one | (fract_bits & signif_bit_mask));
  volatile double t1 = d2 - 1.5;
  volatile double t2 = t1 * 0.289529654;
  volatile double t3 = t2 + 0.176091259;
  volatile double t4 = static_cast<double>(bin_exp) * 0.301029995663981;
  volatile double d = t3 + t4;
  const int64_t d_bits = double_to_bits(d);
  const int exponent =
    static_cast<int>((d_bits & exp_bit_mask) >> exp_shift) - 1023;
  const bool is_negative = (d_bits & sign_bit_mask) != 0;
  if(exponent >= 0 && exponent < 52)
  {
    const int64_t mask = signif_bit_mask >> exponent;
    const int r = static_cast<int>(
      ((d_bits & signif_bit_mask) | fract_hob) >> (exp_shift - exponent));
    return is_negative ? (((mask & d_bits) == 0) ? -r : -r - 1) : r;
  }
  if(exponent < 0)
  {
    return ((d_bits & ~sign_bit_mask) == 0) ? 0 : (is_negative ? -1 : 0);
  }
  return static_cast<int>(static_cast<double>(d));
}

/// The digits and the decimal exponent of BinaryToASCIIBuffer: the value
/// is 0.digits * 10^dec_exponent.
struct buffert
{
  std::string digits;
  int dec_exponent = 0;

  void develop_long_digits(int dec_exp, int64_t lvalue, int insignificant)
  {
    if(insignificant != 0)
    {
      const int64_t pow10 = lshl(long_5_pow(insignificant), insignificant);
      const int64_t residue = lvalue % pow10;
      lvalue /= pow10;
      dec_exp += insignificant;
      if(residue >= (pow10 >> 1))
        ++lvalue;
    }
    // the digits from the last, dropping trailing zeros
    std::string reversed;
    int64_t c = lvalue % 10;
    lvalue /= 10;
    while(c == 0)
    {
      ++dec_exp;
      c = lvalue % 10;
      lvalue /= 10;
    }
    while(lvalue != 0)
    {
      reversed.push_back(static_cast<char>('0' + c));
      ++dec_exp;
      c = lvalue % 10;
      lvalue /= 10;
    }
    reversed.push_back(static_cast<char>('0' + c));
    digits.assign(reversed.rbegin(), reversed.rend());
    dec_exponent = dec_exp + 1;
  }

  void roundup()
  {
    std::size_t i = digits.size() - 1;
    char q = digits[i];
    if(q == '9')
    {
      while(q == '9' && i > 0)
      {
        digits[i] = '0';
        q = digits[--i];
      }
      if(q == '9')
      {
        dec_exponent += 1;
        digits[0] = '1';
        return;
      }
    }
    digits[i] = static_cast<char>(q + 1);
  }

  void dtoa(
    int bin_exp,
    int64_t fract_bits,
    int n_significant_bits,
    bool is_compatible_format);
};

void buffert::dtoa(
  int bin_exp,
  int64_t fract_bits,
  int n_significant_bits,
  bool is_compatible_format)
{
  const int tail_zeros = number_of_trailing_zeros(fract_bits);
  const int n_fract_bits = exp_shift + 1 - tail_zeros;
  const int n_tiny_bits = std::max(0, n_fract_bits - bin_exp - 1);
  if(bin_exp <= max_small_bin_exp && bin_exp >= min_small_bin_exp)
  {
    if(
      n_tiny_bits < long_5_pow_length &&
      n_fract_bits + n_5_bits[n_tiny_bits] < 64 && n_tiny_bits == 0)
    {
      const int insignificant =
        bin_exp > n_significant_bits
          ? insignificant_digits_for_pow2(bin_exp - n_significant_bits - 1)
          : 0;
      if(bin_exp >= exp_shift)
        fract_bits = lshl(fract_bits, bin_exp - exp_shift);
      else
      {
        fract_bits = static_cast<int64_t>(
          static_cast<uint64_t>(fract_bits) >> (exp_shift - bin_exp));
      }
      develop_long_digits(0, fract_bits, insignificant);
      return;
    }
  }

  int dec_exp = estimate_dec_exp(fract_bits, bin_exp);
  const int b5 = std::max(0, -dec_exp);
  int b2 = b5 + n_tiny_bits + bin_exp;
  const int s5 = std::max(0, dec_exp);
  int s2 = s5 + n_tiny_bits;
  const int m5 = b5;
  int m2 = b2 - n_significant_bits;

  fract_bits =
    static_cast<int64_t>(static_cast<uint64_t>(fract_bits) >> tail_zeros);
  b2 -= n_fract_bits - 1;
  const int common2factor = std::min(b2, s2);
  b2 -= common2factor;
  s2 -= common2factor;
  m2 -= common2factor;
  if(n_fract_bits == 1)
    m2 -= 1;
  if(m2 < 0)
  {
    b2 -= m2;
    s2 -= m2;
    m2 = 0;
  }

  bool low;
  bool high;
  int64_t low_digit_difference;
  int q;
  digits.clear();

  const int b_bits =
    n_fract_bits + b2 + (b5 < n_5_bits_length ? n_5_bits[b5] : b5 * 3);
  const int ten_s_bits =
    s2 + 1 + (s5 + 1 < n_5_bits_length ? n_5_bits[s5 + 1] : (s5 + 1) * 3);
  if(b_bits < 64 && ten_s_bits < 64)
  {
    if(b_bits < 32 && ten_s_bits < 32)
    {
      int32_t b =
        ishl(imul(static_cast<int32_t>(fract_bits), small_5_pow(b5)), b2);
      const int32_t s = ishl(small_5_pow(s5), s2);
      int32_t m = ishl(small_5_pow(m5), m2);
      const int32_t tens = imul(s, 10);
      q = b / s;
      b = imul(10, b % s);
      m = imul(m, 10);
      low = b < m;
      high = iadd(b, m) > tens;
      if(q == 0 && !high)
        --dec_exp;
      else
        digits.push_back(static_cast<char>('0' + q));
      if(!is_compatible_format || dec_exp < -3 || dec_exp >= 8)
        high = low = false;
      while(!low && !high)
      {
        q = b / s;
        b = imul(10, b % s);
        m = imul(m, 10);
        if(m > 0)
        {
          low = b < m;
          high = iadd(b, m) > tens;
        }
        else
        {
          low = true;
          high = true;
        }
        digits.push_back(static_cast<char>('0' + q));
      }
      low_digit_difference = isub(ishl(b, 1), tens);
    }
    else
    {
      int64_t b = lshl(lmul(fract_bits, long_5_pow(b5)), b2);
      const int64_t s = lshl(long_5_pow(s5), s2);
      int64_t m = lshl(long_5_pow(m5), m2);
      const int64_t tens = lmul(s, 10);
      q = static_cast<int>(b / s);
      b = lmul(10, b % s);
      m = lmul(m, 10);
      low = b < m;
      high = ladd(b, m) > tens;
      if(q == 0 && !high)
        --dec_exp;
      else
        digits.push_back(static_cast<char>('0' + q));
      if(!is_compatible_format || dec_exp < -3 || dec_exp >= 8)
        high = low = false;
      while(!low && !high)
      {
        q = static_cast<int>(b / s);
        b = lmul(10, b % s);
        m = lmul(m, 10);
        if(m > 0)
        {
          low = b < m;
          high = ladd(b, m) > tens;
        }
        else
        {
          low = true;
          high = true;
        }
        digits.push_back(static_cast<char>('0' + q));
      }
      low_digit_difference = lsub(lshl(b, 1), tens);
    }
  }
  else
  {
    // exact (the normalization of FDBigInteger scales all alike)
    const mp_integer s_val = power(5, s5) * power(2, s2);
    mp_integer b_val = mp_integer{fract_bits} * power(5, b5) * power(2, b2);
    mp_integer m_val = power(5, m5 + 1) * power(2, m2 + 1);
    const mp_integer ten_s_val = power(5, s5 + 1) * power(2, s2 + 1);
    const auto quo_rem = [&]()
    {
      const mp_integer quotient = b_val / s_val;
      b_val = (b_val % s_val) * 10;
      return static_cast<int>(quotient.to_long());
    };
    q = quo_rem();
    low = b_val < m_val;
    high = ten_s_val <= b_val + m_val;
    if(q == 0 && !high)
      --dec_exp;
    else
      digits.push_back(static_cast<char>('0' + q));
    if(!is_compatible_format || dec_exp < -3 || dec_exp >= 8)
      high = low = false;
    while(!low && !high)
    {
      q = quo_rem();
      m_val *= 10;
      low = b_val < m_val;
      high = ten_s_val <= b_val + m_val;
      digits.push_back(static_cast<char>('0' + q));
    }
    if(high && low)
    {
      b_val *= 2;
      low_digit_difference = b_val < ten_s_val ? -1 : b_val > ten_s_val ? 1 : 0;
    }
    else
      low_digit_difference = 0;
  }
  dec_exponent = dec_exp + 1;
  if(high)
  {
    if(low)
    {
      if(low_digit_difference == 0)
      {
        if(((digits.back() - '0') & 1) != 0)
          roundup();
      }
      else if(low_digit_difference > 0)
        roundup();
    }
    else
      roundup();
  }
}

/// getBinaryToASCIIConverter of a finite non-zero double (or float)
buffert
converter(double value, bool single_precision, bool is_compatible_format)
{
  buffert buf;
  if(single_precision)
  {
    const float f = static_cast<float>(value);
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof(bits));
    int32_t fract_bits = static_cast<int32_t>(bits & 0x007FFFFF);
    int bin_exp = static_cast<int>((bits & 0x7F800000) >> 23);
    int n_significant_bits;
    if(bin_exp == 0)
    {
      const int leading_zeros =
        number_of_leading_zeros(static_cast<uint32_t>(fract_bits)) - 32;
      const int shift = leading_zeros - (31 - 23);
      fract_bits = ishl(fract_bits, shift);
      bin_exp = 1 - shift;
      n_significant_bits = 32 - leading_zeros;
    }
    else
    {
      fract_bits |= 1 << 23;
      n_significant_bits = 24;
    }
    bin_exp -= 127;
    buf.dtoa(
      bin_exp,
      lshl(static_cast<int64_t>(fract_bits), exp_shift - 23),
      n_significant_bits,
      true);
    return buf;
  }
  const int64_t d_bits = double_to_bits(value);
  int64_t fract_bits = d_bits & signif_bit_mask;
  int bin_exp = static_cast<int>((d_bits & exp_bit_mask) >> exp_shift);
  int n_significant_bits;
  if(bin_exp == 0)
  {
    const int leading_zeros =
      number_of_leading_zeros(static_cast<uint64_t>(fract_bits));
    const int shift = leading_zeros - (63 - exp_shift);
    fract_bits = lshl(fract_bits, shift);
    bin_exp = 1 - shift;
    n_significant_bits = 64 - leading_zeros;
  }
  else
  {
    fract_bits |= fract_hob;
    n_significant_bits = exp_shift + 1;
  }
  bin_exp -= 1023;
  buf.dtoa(bin_exp, fract_bits, n_significant_bits, is_compatible_format);
  return buf;
}
} // namespace

std::string
legacy_java_digits(double value, bool single_precision, int &exponent)
{
  PRECONDITION(std::isfinite(value) && value > 0);
  const buffert buf = converter(value, single_precision, true);
  exponent = buf.dec_exponent - 1;
  return buf.digits;
}

std::string legacy_java_to_string(double value, bool single_precision)
{
  if(std::isnan(value))
    return "NaN";
  const std::string sign = std::signbit(value) ? "-" : "";
  if(std::isinf(value))
    return sign + "Infinity";
  if(value == 0)
    return sign + "0.0";
  const buffert buf = converter(std::fabs(value), single_precision, true);
  const std::string &d = buf.digits;
  const int n = static_cast<int>(d.size());
  const int e = buf.dec_exponent;
  // BinaryToASCIIBuffer.getChars
  std::string result = sign;
  if(e > 0 && e < 8)
  {
    const int char_length = std::min(n, e);
    result += d.substr(0, static_cast<std::size_t>(char_length));
    if(char_length < e)
      result +=
        std::string(static_cast<std::size_t>(e - char_length), '0') + ".0";
    else
    {
      result += '.';
      result +=
        char_length < n ? d.substr(static_cast<std::size_t>(char_length)) : "0";
    }
  }
  else if(e <= 0 && e > -3)
    result += "0." + std::string(static_cast<std::size_t>(-e), '0') + d;
  else
  {
    result += d.substr(0, 1) + '.' + (n > 1 ? d.substr(1) : "0") + 'E';
    result += std::to_string(e <= 0 ? -(-e + 1) : e - 1);
  }
  return result;
}

namespace
{
/// FormattedFloatingDecimal.applyPrecision: round \p digits (all kept, the
/// rounded ones as zeros) to \p prec digits, half up.
int apply_precision(int dec_exp, std::string &digits, int prec)
{
  const int n = static_cast<int>(digits.size());
  if(prec >= n || prec < 0)
    return dec_exp;
  if(prec == 0)
  {
    if(digits[0] >= '5')
    {
      digits = '1' + std::string(digits.size() - 1, '0');
      return dec_exp + 1;
    }
    digits = std::string(digits.size(), '0');
    return dec_exp;
  }
  char q = digits[static_cast<std::size_t>(prec)];
  if(q >= '5')
  {
    int i = prec;
    q = digits[static_cast<std::size_t>(--i)];
    if(q == '9')
    {
      while(q == '9' && i > 0)
        q = digits[static_cast<std::size_t>(--i)];
      if(q == '9')
      {
        digits = '1' + std::string(digits.size() - 1, '0');
        return dec_exp + 1;
      }
    }
    digits[static_cast<std::size_t>(i)] = static_cast<char>(q + 1);
    std::fill(digits.begin() + i + 1, digits.end(), '0');
  }
  else
    std::fill(digits.begin() + prec, digits.end(), '0');
  return dec_exp;
}

/// FormattedFloatingDecimal.fillDecimal (without a sign)
std::string fill_decimal(int precision, const std::string &digits, int exp)
{
  const int n = static_cast<int>(digits.size());
  if(exp > 0)
  {
    if(n < exp)
      return digits + std::string(static_cast<std::size_t>(exp - n), '0');
    const int t = std::min(n - exp, precision);
    std::string m = digits.substr(0, static_cast<std::size_t>(exp));
    if(t > 0)
      m += '.' + digits.substr(
                   static_cast<std::size_t>(exp), static_cast<std::size_t>(t));
    return m;
  }
  const int zeros = std::max(0, std::min(-exp, precision));
  const int t = std::max(0, std::min(n, precision + exp));
  if(zeros > 0)
  {
    return "0." + std::string(static_cast<std::size_t>(zeros), '0') +
           digits.substr(0, static_cast<std::size_t>(t));
  }
  if(t > 0)
    return "0." + digits.substr(0, static_cast<std::size_t>(t));
  return "0";
}

/// FormattedFloatingDecimal.fillScientific (without a sign): the mantissa,
/// and the exponent with its sign and at least two digits
std::pair<std::string, std::string>
fill_scientific(int precision, const std::string &digits, int exp)
{
  const int n = static_cast<int>(digits.size());
  const int t = std::max(0, std::min(n - 1, precision));
  std::string mantissa = digits.substr(0, 1);
  if(t > 0)
    mantissa += '.' + digits.substr(1, static_cast<std::size_t>(t));
  const bool negative = exp <= 0;
  const int e = negative ? -exp + 1 : exp - 1;
  std::string magnitude = std::to_string(e);
  if(magnitude.size() < 2)
    magnitude.insert(0, 1, '0');
  return {mantissa, (negative ? "-" : "+") + magnitude};
}

/// Formatter.addZeros
void add_zeros(std::string &mantissa, int prec)
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
} // namespace

std::string
legacy_java_format_float(double value, char conversion, int precision)
{
  if(std::isnan(value))
    return "NaN";
  const std::string sign = std::signbit(value) ? "-" : "";
  const double v = std::fabs(value);
  if(std::isinf(v))
    return sign + "Infinity";
  // the digits of FloatingDecimal (not in the compatible format), or 0
  std::string digits = "0";
  int dec_exp = 0;
  if(v != 0)
  {
    const buffert buf = converter(v, false, false);
    digits = buf.digits;
    dec_exp = buf.dec_exponent;
  }
  if(conversion == 'e')
  {
    const int prec = precision == -1 ? 6 : precision;
    const int exp = apply_precision(dec_exp, digits, prec + 1);
    auto parts = fill_scientific(prec, digits, exp);
    add_zeros(parts.first, prec);
    return sign + parts.first + 'e' + (v == 0 ? "+00" : parts.second);
  }
  if(conversion == 'f')
  {
    const int prec = precision == -1 ? 6 : precision;
    const int exp = apply_precision(dec_exp, digits, dec_exp + prec);
    std::string mantissa = fill_decimal(prec, digits, exp);
    add_zeros(mantissa, prec);
    return sign + mantissa;
  }
  PRECONDITION(conversion == 'g');
  int prec = precision == -1 ? 6 : precision == 0 ? 1 : precision;
  std::string mantissa;
  std::string exponent;
  if(v == 0)
  {
    mantissa = "0";
    prec -= 1;
  }
  else
  {
    const int exp = apply_precision(dec_exp, digits, prec);
    if(exp - 1 < -4 || exp - 1 >= prec)
    {
      auto parts = fill_scientific(prec - 1, digits, exp);
      mantissa = parts.first;
      exponent = parts.second;
      prec -= 1;
    }
    else
    {
      mantissa = fill_decimal(prec - exp, digits, exp);
      // Formatter: prec -= getExponentRounded() + 1
      prec -= exp;
    }
  }
  add_zeros(mantissa, prec);
  return sign + mantissa + (exponent.empty() ? "" : 'e' + exponent);
}
