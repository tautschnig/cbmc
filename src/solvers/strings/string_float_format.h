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
