/*******************************************************************\

Module: Unit tests for format_float

Author: Diffblue Ltd.

\*******************************************************************/

#include <solvers/strings/string_float_format.h>
#include <testing-utils/use_catch.h>

#include <cmath>
#include <limits>

// The expected strings are those of Java 25 and CPython 3.12.
TEST_CASE(
  "format_float as Java's Float.toString",
  "[core][solvers][strings][string_float_format]")
{
  const auto java_float = [](float f)
  { return format_float(f, true, float_formatt::JAVA); };
  REQUIRE(java_float(1.5f) == "1.5");
  REQUIRE(java_float(-1.5f) == "-1.5");
  REQUIRE(java_float(0.1f) == "0.1");
  REQUIRE(java_float(1.0f / 3) == "0.33333334");
  REQUIRE(java_float(123456.7f) == "123456.7");
  REQUIRE(java_float(2097152.25f) == "2097152.2");
  REQUIRE(java_float(9999999.0f) == "9999999.0");
  REQUIRE(java_float(1e7f) == "1.0E7");
  REQUIRE(java_float(0.001f) == "0.001");
  REQUIRE(java_float(1e30f) == "1.0E30");
  REQUIRE(java_float(-1.5e10f) == "-1.5E10");
  REQUIRE(java_float(5.67e-9f) == "5.67E-9");
  // Where one digit would do, Java gives the closest of two.
  REQUIRE(java_float(std::numeric_limits<float>::denorm_min()) == "1.4E-45");
  REQUIRE(
    java_float(7 * std::numeric_limits<float>::denorm_min()) == "9.8E-45");
  REQUIRE(java_float(0.0f) == "0.0");
  REQUIRE(java_float(-0.0f) == "-0.0");
  REQUIRE(java_float(std::numeric_limits<float>::infinity()) == "Infinity");
  REQUIRE(java_float(-std::numeric_limits<float>::infinity()) == "-Infinity");
  REQUIRE(java_float(std::nanf("")) == "NaN");
}

TEST_CASE(
  "format_float as Java's Double.toString",
  "[core][solvers][strings][string_float_format]")
{
  const auto java_double = [](double d)
  { return format_float(d, false, float_formatt::JAVA); };
  REQUIRE(java_double(1.0 / 3) == "0.3333333333333333");
  REQUIRE(java_double(0.001) == "0.001");
  REQUIRE(java_double(1e-5) == "1.0E-5");
  REQUIRE(java_double(1e7) == "1.0E7");
  REQUIRE(java_double(std::numeric_limits<double>::denorm_min()) == "4.9E-324");
  REQUIRE(
    java_double(std::numeric_limits<double>::max()) ==
    "1.7976931348623157E308");
}

TEST_CASE(
  "format_float in Java's notations of the magnitude",
  "[core][solvers][strings][string_float_format]")
{
  REQUIRE(format_float(-1.5f, true, float_formatt::JAVA_PLAIN) == "1.5");
  REQUIRE(
    format_float(1e30f, true, float_formatt::JAVA_PLAIN) ==
    "1000000000000000000000000000000.0");
  REQUIRE(
    format_float(-1.5e10f, true, float_formatt::JAVA_SCIENTIFIC) == "1.5E10");
  REQUIRE(format_float(0.5f, true, float_formatt::JAVA_SCIENTIFIC) == "5.0E-1");
}

TEST_CASE(
  "format_float as Python's repr",
  "[core][solvers][strings][string_float_format]")
{
  const auto python = [](double d)
  { return format_float(d, false, float_formatt::PYTHON); };
  REQUIRE(python(1.0) == "1.0");
  REQUIRE(python(-1.5) == "-1.5");
  REQUIRE(python(0.1) == "0.1");
  REQUIRE(python(1.0 / 3) == "0.3333333333333333");
  REQUIRE(python(1e15) == "1000000000000000.0");
  REQUIRE(python(1e16) == "1e+16");
  REQUIRE(python(2.5e16) == "2.5e+16");
  REQUIRE(python(123456789012345.6) == "123456789012345.6");
  REQUIRE(python(1e-4) == "0.0001");
  REQUIRE(python(1e-5) == "1e-05");
  // Python keeps one digit where one would do.
  REQUIRE(python(std::numeric_limits<double>::denorm_min()) == "5e-324");
  REQUIRE(python(-0.0) == "-0.0");
  REQUIRE(python(std::numeric_limits<double>::infinity()) == "inf");
  REQUIRE(python(-std::numeric_limits<double>::infinity()) == "-inf");
  REQUIRE(python(std::nan("")) == "nan");
}

TEST_CASE(
  "float_string_length_bounds",
  "[core][solvers][strings][string_float_format]")
{
  // The longest strings: those of the smallest values in plain notation, and
  // of negative subnormals with 17 or 9 digits.
  REQUIRE(
    float_string_length_bounds(false, float_formatt::JAVA) ==
    std::make_pair(std::size_t{3}, std::size_t{24}));
  REQUIRE(
    float_string_length_bounds(true, float_formatt::JAVA) ==
    std::make_pair(std::size_t{3}, std::size_t{15}));
  REQUIRE(
    float_string_length_bounds(false, float_formatt::PYTHON).second == 24);
  REQUIRE(
    format_float(-2.2250738585072014e-308, false, float_formatt::JAVA).size() ==
    24);
  REQUIRE(
    format_float(-std::numeric_limits<float>::min(), true, float_formatt::JAVA)
      .size() <= 15);
}

TEST_CASE("parse_float_string", "[core][solvers][strings][string_float_format]")
{
  REQUIRE(parse_float_string("2.75", true, float_formatt::JAVA) == 2.75);
  REQUIRE(parse_float_string("-1.5E10", true, float_formatt::JAVA) == -1.5e10f);
  REQUIRE(
    parse_float_string("0.3333333333333333", false, float_formatt::PYTHON) ==
    1.0 / 3);
  REQUIRE(std::isnan(*parse_float_string("NaN", true, float_formatt::JAVA)));
  // Strings that no value gives: other digits than the shortest, another
  // notation, or not a number.
  REQUIRE_FALSE(parse_float_string("2.750", true, float_formatt::JAVA));
  REQUIRE_FALSE(parse_float_string("1.5e10", true, float_formatt::JAVA));
  REQUIRE_FALSE(parse_float_string("15.0E9", true, float_formatt::JAVA));
  REQUIRE_FALSE(parse_float_string("1e16", false, float_formatt::PYTHON));
  REQUIRE_FALSE(parse_float_string("-1.5", true, float_formatt::JAVA_PLAIN));
  REQUIRE_FALSE(parse_float_string("abc", false, float_formatt::JAVA));
}

TEST_CASE(
  "float_string_viable_prefix",
  "[core][solvers][strings][string_float_format]")
{
  REQUIRE(float_string_viable_prefix("1.5E-3", float_formatt::JAVA) == 6);
  REQUIRE(float_string_viable_prefix("1.5E-3", float_formatt::JAVA_PLAIN) == 3);
  REQUIRE(float_string_viable_prefix("1.E3", float_formatt::JAVA) == 2);
  REQUIRE(float_string_viable_prefix("-1.5", float_formatt::JAVA_PLAIN) == 0);
  REQUIRE(float_string_viable_prefix("--1", float_formatt::JAVA) == 1);
  REQUIRE(float_string_viable_prefix("NaNx", float_formatt::JAVA) == 3);
  REQUIRE(float_string_viable_prefix("1e+16", float_formatt::PYTHON) == 5);
  REQUIRE(float_string_viable_prefix("1e+16", float_formatt::JAVA) == 1);
  REQUIRE(float_string_viable_prefix("-inf", float_formatt::PYTHON) == 4);
}
