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

/// The symbols of the numbers that java.util.Formatter prints by a locale.
struct java_number_symbolst
{
  /// The zero digit, of the digits.
  unsigned zero;
  /// The decimal separator.
  unsigned decimal;
  /// The grouping separator of the `,` flag.
  unsigned grouping;
  /// The size of the groups of the `,` flag, 0 for a locale without
  /// grouping: Java 11 and later print no separators, and Java 8 (CLDR), 9
  /// and 10 throw an ArithmeticException for an integer part of two digits
  /// or more (a division by the size).
  unsigned group_size;

  bool operator==(const java_number_symbolst &other) const
  {
    return zero == other.zero && decimal == other.decimal &&
           grouping == other.grouping && group_size == other.group_size;
  }
};

/// What a set of possible locales changes.
struct java_locale_symbolst
{
  /// The possible symbols of the numbers of Formatter, or none for any.
  std::optional<std::vector<java_number_symbolst>> numbers;
  /// The possible case conversions.
  std::vector<java_case_familyt> cases;

  /// Whether the digits and the decimal separator are those of ASCII: '0'
  /// and '.'.
  bool numbers_are_ascii() const
  {
    if(!numbers.has_value())
      return false;
    for(const java_number_symbolst &n : *numbers)
    {
      if(n.zero != 0x30 || n.decimal != 0x2e)
        return false;
    }
    return true;
  }

  /// Whether the case conversion is that of Locale.ROOT.
  bool cases_are_root() const
  {
    return cases.size() == 1 && cases.front() == java_case_familyt::ROOT;
  }
};

/// What the locale of the language tag \p tag changes, as measured for
/// Java 8 to 25 with their locale providers (see locale_numbers): for a tag
/// that they do not know, or with a Unicode extension (as -u-nu-thai), any
/// numbers.
java_locale_symbolst java_locale_symbols(const std::string &tag);

/// What any locale changes: any numbers, and all the case conversions.
java_locale_symbolst java_any_locale_symbols();

/// What the default locale of the runtime changes (see
/// configt::javat::locale).
java_locale_symbolst java_default_locale_symbols();

#endif // CPROVER_SOLVERS_STRINGS_JAVA_LOCALE_H
