/*******************************************************************\

Module: String representations of floating-point values

Author: Diffblue Ltd.

\*******************************************************************/

/// \file
/// String representations of floating-point values, as Java's
/// Float.toString and Double.toString and Python's repr give them.

#ifndef CPROVER_SOLVERS_STRINGS_STRING_FLOAT_FORMAT_H
#define CPROVER_SOLVERS_STRINGS_STRING_FLOAT_FORMAT_H

#include <string>

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

#endif // CPROVER_SOLVERS_STRINGS_STRING_FLOAT_FORMAT_H
