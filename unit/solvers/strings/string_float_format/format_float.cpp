/*******************************************************************\

Module: Unit tests for format_float

Author: Diffblue Ltd.

\*******************************************************************/

#include <solvers/strings/string_float_format.h>
#include <testing-utils/use_catch.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <tuple>
#include <vector>

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

// The expected strings are those of String.format of Java 21 and 25, with
// the bits of the double, the conversion and the precision (-1 for none).
TEST_CASE(
  "java_format_float as java.util.Formatter",
  "[core][solvers][strings][string_float_format]")
{
  const std::vector<std::tuple<std::uint64_t, char, int, std::string>> cases{
    {0x0000000000000000ULL, 'e', -1, "0.000000e+00"}, // %e of 0.0
    {0x8000000000000000ULL, 'f', -1, "-0.000000"},    // %f of -0.0
    {0x3ff0000000000000ULL, 'g', -1, "1.00000"},      // %g of 1.0
    {0xbff0000000000000ULL, 'a', -1, "-0x1.0p0"},     // %a of -1.0
    {0x3fe0000000000000ULL, 'e', 0, "5e-01"},         // %.0e of 0.5
    {0x3fc0000000000000ULL, 'f', 0, "0"},             // %.0f of 0.125
    {0x3ff0147ae147ae14ULL, 'g', 0, "1"},             // %.0g of 1.005
    {0x4005666666666666ULL, 'a', 0, "0x1.5p1"},       // %.0a of 2.675
    {0x44b52d02c7e14af6ULL, 'e', 1, "1.0e+23"},       // %.1e of 1e+23
    {0x44b52d02c7e14af6ULL,
     'f',
     1,
     "100000000000000000000000.0"},            // %.1f of 1e+23
    {0x0000000000000001ULL, 'g', 1, "5e-324"}, // %.1g of 5e-324
    {0x0010000000000000ULL,
     'a',
     1,
     "0x1.0p-1022"},                         // %.1a of 2.2250738585072014e-308
    {0x000fffffffffffffULL, 'f', 2, "0.00"}, // %.2f of 2.225073858507201e-308
    {0x7fefffffffffffffULL,
     'e',
     3,
     "1.798e+308"}, // %.3e of 1.7976931348623157e+308
    {0x7ff0000000000000ULL, 'g', 3, "Infinity"},              // %.3g of inf
    {0xfff0000000000000ULL, 'a', 5, "-Infinity"},             // %.5a of -inf
    {0x7ff8000000000000ULL, 'f', 10, "NaN"},                  // %.10f of nan
    {0x3fb999999999999aULL, 'e', 12, "1.000000000000e-01"},   // %.12e of 0.1
    {0x3fc999999999999aULL, 'a', 13, "0x1.999999999999ap-3"}, // %.13a of 0.2
    {0x3fd3333333333333ULL, 'g', 15, "0.300000000000000"},    // %.15g of 0.3
    {0x3fd5555555555555ULL,
     'f',
     17,
     "0.33333333333333330"}, // %.17f of 0.3333333333333333
    {0x3fe5555555555555ULL,
     'e',
     20,
     "6.66666666666666600000e-01"}, // %.20e of 0.6666666666666666
    {0x40fe240c9fbe76c9ULL,
     'f',
     30,
     "123456.789000000000000000000000000000"},        // %.30f of 123456.789
    {0x4023ffffef39085fULL, 'e', -1, "1.000000e+01"}, // %e of 9.9999995
    {0x4023000000000000ULL, 'f', -1, "9.500000"},     // %f of 9.5
    {0x3fee666666666666ULL, 'g', -1, "0.950000"},     // %g of 0.95
    {0x3f8374bc6a7ef9dbULL, 'a', -1, "0x1.374bc6a7ef9dbp-7"}, // %a of 0.0095
    {0x4058ffae147ae148ULL, 'e', 0, "1e+02"},                 // %.0e of 99.995
    {0x3ee4f8b588e368f1ULL, 'f', 0, "0"},                     // %.0f of 1e-05
    {0x3f1a36e2eb1c432dULL, 'g', 0, "0.0001"},                // %.0g of 0.0001
    {0x3f50624dd2f1a9fcULL, 'a', 0, "0x1.0p-10"},             // %.0a of 0.001
    {0x3ee4f8b588e368f1ULL, 'e', 1, "1.0e-05"},               // %.1e of 1e-05
    {0x416312d000000000ULL, 'f', 1, "10000000.0"},    // %.1f of 10000000.0
    {0x4341c37937e08000ULL, 'g', 1, "1e+16"},         // %.1g of 1e+16
    {0x4376345785d8a000ULL, 'a', 1, "0x1.6p56"},      // %.1a of 1e+17
    {0x4011666666666666ULL, 'f', 2, "4.35"},          // %.2f of 4.35
    {0x3fa70a3d70a3d70aULL, 'e', 3, "4.500e-02"},     // %.3e of 0.045
    {0x3ff7333333333333ULL, 'g', 3, "1.45"},          // %.3g of 1.45
    {0x4004000000000000ULL, 'a', 5, "0x1.40000p1"},   // %.5a of 2.5
    {0x400c000000000000ULL, 'f', 10, "3.5000000000"}, // %.10f of 3.5
    {0x3fa999999999999aULL, 'e', 12, "5.000000000000e-02"},   // %.12e of 0.05
    {0x3fc3333333333333ULL, 'a', 13, "0x1.3333333333333p-3"}, // %.13a of 0.15
    {0x3fd0000000000000ULL, 'g', 15, "0.250000000000000"},    // %.15g of 0.25
    {0x3fd6666666666666ULL, 'f', 17, "0.35000000000000000"},  // %.17f of 0.35
    {0x3f1a36d1bd105b06ULL,
     'e',
     20,
     "9.99990000000000000000e-05"}, // %.20e of 9.9999e-05
    {0x412e847f00000000ULL,
     'f',
     30,
     "999999.500000000000000000000000000000"},        // %.30f of 999999.5
    {0x416312cf60000000ULL, 'e', -1, "9.999995e+06"}, // %e of 9999995.0
    {0x3f1a368d04e0ba6aULL, 'f', -1, "0.000100"},     // %f of 9.9995e-05
    {0x40fd2f8c5ac57bf0ULL, 'g', -1, "119545"},       // %g of 119544.7721609918
    {0x636d3385a46cf86bULL,
     'a',
     5,
     "0x1.d3386p567"}, // %.5a of 8.816356026746655e+170
    {0x412546259bc767c4ULL, 'f', 2, "697106.80"}, // %.2f of 697106.804255717
    {0x411a1df2e90f0ba6ULL,
     'f',
     17,
     "427900.72759645653000000"},                // %.17f of 427900.72759645653
    {0x411c3e9503b5bc92ULL, 'e', 0, "5e+05"},    // %.0e of 462757.2536229576
    {0xc0395694467381d8ULL, 'a', 0, "-0x1.9p4"}, // %.0a of -25.3382
    {0x43d30582a0000000ULL,
     'f',
     17,
     "5482581141242249000.00000000000000000"}, // %.17f of 5.482581141242249e+18
    {0x4080bee1a93293d1ULL, 'e', 0, "5e+02"},  // %.0e of 535.860186
    {0x4e8be9503f71f526ULL,
     'f',
     10,
     "24079658752493050000000000000000000000000000000000000000000000000000000."
     "0000000000"}, // %.10f of 2.407965875249305e+70
    {0x71145511e51b25fcULL,
     'f',
     2,
     "5171817724638751000000000000000000000000000000000000000000000000000000000"
     "0000000000000000000000000000000000000000000000000000000000000000000000000"
     "0000000000000000000000000000000000000000000000000000000000000000000000000"
     "000000000000000000.00"}, // %.2f of 5.171817724638751e+236
    {0x408b3e4a78af3e47ULL, 'e', -1, "8.717864e+02"}, // %e of 871.786363
    {0x9506e3ef059ab80dULL,
     'e',
     20,
     "-2.22805409934027880000e-207"}, // %.20e of -2.2280540993402788e-207
    {0x4070f428f5c28f5cULL, 'g', -1, "271.260"}, // %g of 271.26
    {0xd2ecf093b708eecdULL,
     'f',
     0,
     "-294756599729629280000000000000000000000000000000000000000000000000000000"
     "00000000000000000000"}, // %.0f of -2.9475659972962928e+91
    {0xc10ccf245b2ee2f8ULL,
     'a',
     13,
     "-0x1.ccf245b2ee2f8p17"}, // %.13a of -236004.54452302284
    {0x407aa00000000000ULL, 'f', -1, "426.000000"}, // %f of 426.0
    {0x408406c61f9f01b8ULL, 'f', 1, "640.8"},       // %.1f of 640.84674
    {0x8347be7d6bf100efULL,
     'e',
     -1,
     "-7.435496e-293"},                         // %e of -7.435496332362564e-293
    {0x4088711eb851eb85ULL, 'e', 1, "7.8e+02"}, // %.1e of 782.14
    {0xc128d81d69158dfcULL,
     'a',
     5,
     "-0x1.8d81dp19"}, // %.5a of -814094.7052425737
    {0x7d56620e9c864873ULL,
     'g',
     15,
     "5.71813944398297e+295"}, // %.15g of 5.71813944398297e+295
    {0xc064000000000000ULL, 'f', 2, "-160.00"}, // %.2f of -160.0
    {0xc123c01bd892d4adULL,
     'f',
     30,
     "-647181.922995229800000000000000000000"}, // %.30f of -647181.9229952298
    {0x0ce8660a2ca3d023ULL,
     'e',
     3,
     "1.745e-246"}, // %.3e of 1.7447706131299013e-246
    {0x41f7c045523cc38dULL,
     'f',
     2,
     "6375626019.80"}, // %.2f of 6375626019.797742
    {0xc1279cededd2325cULL,
     'a',
     13,
     "-0x1.79cededd2325cp19"}, // %.13a of -773750.9644942987
  };
  for(const auto &c : cases)
  {
    double d;
    const std::uint64_t bits = std::get<0>(c);
    std::memcpy(&d, &bits, sizeof(d));
    INFO("conversion " << std::get<1>(c) << ", precision " << std::get<2>(c));
    REQUIRE(
      java_format_float(d, std::get<1>(c), std::get<2>(c)) == std::get<3>(c));
  }
}

TEST_CASE(
  "java_format_float_preimage",
  "[core][solvers][strings][string_float_format]")
{
  // the doubles that %.2f prints as 3.14, and as -0.00
  const auto pi = java_format_float_preimage("3.14", 'f', 2);
  REQUIRE_FALSE(pi.empty);
  REQUIRE_FALSE(pi.negative);
  REQUIRE(java_format_float(pi.low, 'f', 2) == "3.14");
  REQUIRE(java_format_float(std::nextafter(pi.low, 0.0), 'f', 2) == "3.13");
  REQUIRE(java_format_float(pi.high, 'f', 2) == "3.14");
  REQUIRE(java_format_float(std::nextafter(pi.high, 4.0), 'f', 2) == "3.15");
  const auto zero = java_format_float_preimage("-0.00", 'f', 2);
  REQUIRE_FALSE(zero.empty);
  REQUIRE(zero.negative);
  REQUIRE(zero.low == 0.0);
  REQUIRE(java_format_float_preimage("NaN", 'e', -1).nan);
  const auto infinity = java_format_float_preimage("Infinity", 'a', -1);
  REQUIRE(std::isinf(infinity.low));
  // texts that no double gives: other digits, another notation, or not a
  // number
  REQUIRE(java_format_float_preimage("3.1", 'f', 2).empty);
  REQUIRE(java_format_float_preimage("3.14e+00", 'f', 2).empty);
  REQUIRE(java_format_float_preimage("0x1.8p1", 'e', -1).empty);
  REQUIRE(java_format_float_preimage("abc", 'g', -1).empty);
}

TEST_CASE(
  "java_format_float_lengths",
  "[core][solvers][strings][string_float_format]")
{
  // NaN, Infinity, -Infinity, and 1.000000e+00 with a sign or an exponent
  // of 3 digits
  const std::vector<std::pair<std::size_t, std::size_t>> e{
    {3, 3}, {8, 9}, {12, 14}};
  REQUIRE(java_format_float_lengths('e', -1) == e);
  // 1 to 309 digits before the point and 2 after it, a sign, or NaN
  const std::vector<std::pair<std::size_t, std::size_t>> f{{3, 313}};
  REQUIRE(java_format_float_lengths('f', 2) == f);
}
