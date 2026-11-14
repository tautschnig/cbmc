/*******************************************************************\

Module: Unit tests for java_locale_symbols

Author: Diffblue Ltd.

\*******************************************************************/

#include <solvers/strings/java_locale.h>
#include <testing-utils/use_catch.h>

// The expected symbols are those of Java 8 to 25 with the locale providers
// CLDR and COMPAT.
TEST_CASE("java_locale_symbols", "[core][solvers][strings][java_locale]")
{
  using pairs = std::vector<std::pair<unsigned, unsigned>>;
  const auto en = java_locale_symbols("en-US");
  REQUIRE(en.numbers_are_ascii());
  REQUIRE(en.cases_are_root());
  // a comma, and tags are case-insensitive, with _ as of Locale.toString
  REQUIRE(java_locale_symbols("de_de").numbers == pairs{{0x30, 0x2c}});
  // Arabic-Indic digits from Java 11 on, ASCII ones in Java 8 and COMPAT
  REQUIRE(
    java_locale_symbols("ar-EG").numbers ==
    pairs{{0x30, 0x2e}, {0x660, 0x66b}});
  // a region that the data do not have falls back to the language
  REQUIRE(java_locale_symbols("de-XX").numbers == pairs{{0x30, 0x2c}});
  // an extension may select any digits, and an unknown language any data
  REQUIRE_FALSE(java_locale_symbols("en-US-u-nu-thai").numbers.has_value());
  REQUIRE_FALSE(java_locale_symbols("qqq").numbers.has_value());
  // the case conversions of tr and az, and of lt
  REQUIRE(
    java_locale_symbols("tr-TR").cases ==
    std::vector<java_case_familyt>{java_case_familyt::TURKIC});
  REQUIRE(
    java_locale_symbols("az-Latn").cases ==
    std::vector<java_case_familyt>{java_case_familyt::TURKIC});
  REQUIRE(
    java_locale_symbols("lt").cases ==
    std::vector<java_case_familyt>{java_case_familyt::LITHUANIAN});
  const auto any = java_any_locale_symbols();
  REQUIRE_FALSE(any.numbers.has_value());
  REQUIRE(any.cases.size() == 3);
}
