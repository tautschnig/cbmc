/*******************************************************************\

Module: The locales of the Java runtime

Author: Diffblue Ltd.

\*******************************************************************/

/// \file
/// What the locales of the Java runtime change in the string functions that
/// the string solver models: the digits and the decimal separator of
/// java.util.Formatter, and the case conversion of String.toUpperCase and
/// String.toLowerCase.

#ifndef CPROVER_SOLVERS_STRINGS_JAVA_LOCALE_H
#define CPROVER_SOLVERS_STRINGS_JAVA_LOCALE_H

#include <optional>
#include <string>
#include <utility>
#include <vector>

/// The case conversions of Java's locales: String.toUpperCase(Locale) and
/// toLowerCase(Locale) convert as Locale.ROOT does but for the languages
/// tr and az (dotted and dotless i) and lt (the dot above of i and j).
enum class java_case_familyt
{
  ROOT,
  TURKIC,
  LITHUANIAN
};

/// What a set of possible locales changes.
struct java_locale_symbolst
{
  /// The possible pairs of the zero digit and the decimal separator of
  /// Formatter, or none for any characters.
  std::optional<std::vector<std::pair<unsigned, unsigned>>> numbers;
  /// The possible case conversions.
  std::vector<java_case_familyt> cases;

  /// Whether the numbers are those of ASCII: '0' and '.'.
  bool numbers_are_ascii() const
  {
    return numbers.has_value() && numbers->size() == 1 &&
           numbers->front() == std::make_pair(0x30u, 0x2eu);
  }

  /// Whether the case conversion is that of Locale.ROOT.
  bool cases_are_root() const
  {
    return cases.size() == 1 && cases.front() == java_case_familyt::ROOT;
  }
};

/// What the locale of the language tag \p tag changes, as measured for
/// Java 8 to 25 with their locale providers (CLDR and COMPAT): for a tag
/// that they do not know, or with a Unicode extension (as -u-nu-thai), any
/// numbers.
java_locale_symbolst java_locale_symbols(const std::string &tag);

/// What any locale changes: any numbers, and all the case conversions.
java_locale_symbolst java_any_locale_symbols();

/// What the default locale of the runtime changes (see
/// configt::javat::locale).
java_locale_symbolst java_default_locale_symbols();

#endif // CPROVER_SOLVERS_STRINGS_JAVA_LOCALE_H
