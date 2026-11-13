/*******************************************************************\

Module: String representations of floating-point values

Author: Diffblue Ltd.

\*******************************************************************/

/// \file
/// String representations of floating-point values, as Java's
/// Float.toString and Double.toString and Python's repr give them.

#ifndef CPROVER_SOLVERS_STRINGS_STRING_FLOAT_FORMAT_H
#define CPROVER_SOLVERS_STRINGS_STRING_FLOAT_FORMAT_H

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

/// Which representation of a floating-point value to give.
enum class float_formatt
{
  /// The digits of Java's Float.toString or Double.toString in plain
  /// notation (as for magnitudes in [1e-3, 1e7)), of the magnitude.
  JAVA_PLAIN,
  /// The digits of Java's Float.toString or Double.toString in computerized
  /// scientific notation (as for other magnitudes), of the magnitude.
  JAVA_SCIENTIFIC,
  /// Java's Double.toString (or Float.toString), including the sign and the
  /// special values.
  JAVA,
  /// Python's repr of a float.
  PYTHON
};

/// The representation of \p value in \p format. \p single_precision says
/// whether \p value is a float, whose shortest digits that distinguish it
/// are those among floats.
std::string
format_float(double value, bool single_precision, float_formatt format);

/// What java.util.Formatter of Java 21 and later prints for \p value with
/// the conversion \p conversion ('e', 'f', 'g' or 'a') and the precision
/// \p precision (-1 for none), without flags or width, in lower case. (Java
/// 17 takes other digits for some values, as for Double.toString.)
std::string java_format_float(double value, char conversion, int precision);

/// The largest length of java_format_float with \p conversion and
/// \p precision (an upper bound).
std::size_t java_format_float_max_length(char conversion, int precision);

/// The lengths of java_format_float of the doubles with \p conversion and
/// \p precision, as disjoint intervals [first, second] in order.
std::vector<std::pair<std::size_t, std::size_t>>
java_format_float_lengths(char conversion, int precision);

/// The doubles whose java_format_float is a text (see
/// java_format_float_preimage): any NaN, or those of a sign whose magnitude
/// is in [low, high], or none.
struct java_format_float_preimaget
{
  bool nan = false;
  bool empty = true;
  bool negative = false;
  double low = 0;
  double high = 0;
};

/// The doubles whose java_format_float with \p conversion and \p precision
/// is \p text.
java_format_float_preimaget java_format_float_preimage(
  const std::string &text,
  char conversion,
  int precision);

/// The smallest and the largest length of format_float of any value of the
/// precision in \p format, special values included.
std::pair<std::size_t, std::size_t>
float_string_length_bounds(bool single_precision, float_formatt format);

/// The value that \p text represents in \p format, if \p text is
/// format_float of a value of the precision: that value, or for "NaN" and
/// "nan" any NaN.
std::optional<double> parse_float_string(
  const std::string &text,
  bool single_precision,
  float_formatt format);

/// The length of the longest prefix of \p text that is a prefix of a string
/// of the shape of format_float in \p format: digits, a point and digits,
/// or the scientific notation of the format, after a sign in the signed
/// formats, or a special value. A string longer than that is not
/// format_float of any value, nor is any string that starts with one
/// character more of \p text.
std::size_t
float_string_viable_prefix(const std::string &text, float_formatt format);

#endif // CPROVER_SOLVERS_STRINGS_STRING_FLOAT_FORMAT_H
