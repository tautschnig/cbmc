/*******************************************************************\

Module: The strings of floating-point values of Java 18 and earlier

Author: Diffblue Ltd.

\*******************************************************************/

/// \file
/// The strings of floating-point values of Java 18 and earlier (for
/// Double.toString and Float.toString) and of Java 20 and earlier (for
/// java.util.Formatter), which take the digits of the algorithm of
/// jdk.internal.math.FloatingDecimal rather than the shortest ones.

#ifndef CPROVER_SOLVERS_STRINGS_JAVA_FLOATING_DECIMAL_H
#define CPROVER_SOLVERS_STRINGS_JAVA_FLOATING_DECIMAL_H

#include <string>

/// The digits of Double.toString (or Float.toString if \p single_precision)
/// of Java 18 and earlier of a finite positive \p value, and in
/// \p exponent the decimal exponent of the first.
std::string
legacy_java_digits(double value, bool single_precision, int &exponent);

/// Double.toString (or Float.toString if \p single_precision, of the float
/// \p value) of Java 18 and earlier.
std::string legacy_java_to_string(double value, bool single_precision);

/// What java.util.Formatter of Java 20 and earlier prints for \p value with
/// the conversion \p conversion ('e', 'f' or 'g') and the precision
/// \p precision (-1 for none), without flags or width, in lower case.
std::string
legacy_java_format_float(double value, char conversion, int precision);

#endif // CPROVER_SOLVERS_STRINGS_JAVA_FLOATING_DECIMAL_H
