/// \file
/// Unit tests for the python complex-literal string parser.
///
/// Author: Diffblue Ltd.

#include <python/python_complex_parser.h>
#include <testing-utils/use_catch.h>

#include <cmath>

namespace
{
// Convenience: a matcher against `(real, imag)` ignoring tiny rounding.
struct expected_t
{
  double real;
  double imag;
};

bool same(double a, double b)
{
  if(std::isnan(a))
    return std::isnan(b);
  if(std::isinf(a) || std::isinf(b))
    return a == b;
  // Exact comparison — these are constants from constant strings, no
  // arithmetic should reduce precision.
  return a == b;
}

bool matches(
  const std::optional<std::pair<double, double>> &got,
  const expected_t &exp)
{
  return got.has_value() && same(got->first, exp.real) &&
         same(got->second, exp.imag);
}
} // namespace

TEST_CASE(
  "parse_python_complex_string: pure real numbers",
  "[core][python][complex_parser]")
{
  REQUIRE(matches(parse_python_complex_string("0"), {0.0, 0.0}));
  REQUIRE(matches(parse_python_complex_string("42"), {42.0, 0.0}));
  REQUIRE(matches(parse_python_complex_string("-3.5"), {-3.5, 0.0}));
  REQUIRE(matches(parse_python_complex_string("1e2"), {100.0, 0.0}));
  REQUIRE(matches(parse_python_complex_string("-0.0"), {-0.0, 0.0}));
  REQUIRE(matches(parse_python_complex_string("+5"), {5.0, 0.0}));
}

TEST_CASE(
  "parse_python_complex_string: pure imaginary numbers",
  "[core][python][complex_parser]")
{
  REQUIRE(matches(parse_python_complex_string("j"), {0.0, 1.0}));
  REQUIRE(matches(parse_python_complex_string("+j"), {0.0, 1.0}));
  REQUIRE(matches(parse_python_complex_string("-j"), {0.0, -1.0}));
  REQUIRE(matches(parse_python_complex_string("2j"), {0.0, 2.0}));
  REQUIRE(matches(parse_python_complex_string("-3.5j"), {0.0, -3.5}));
  REQUIRE(matches(parse_python_complex_string("0j"), {0.0, 0.0}));
  REQUIRE(matches(parse_python_complex_string(".5j"), {0.0, 0.5}));
  REQUIRE(matches(parse_python_complex_string("J"), {0.0, 1.0}));
}

TEST_CASE(
  "parse_python_complex_string: combined real+imag",
  "[core][python][complex_parser]")
{
  REQUIRE(matches(parse_python_complex_string("1+2j"), {1.0, 2.0}));
  REQUIRE(matches(parse_python_complex_string("1-2j"), {1.0, -2.0}));
  REQUIRE(matches(parse_python_complex_string("-1+2j"), {-1.0, 2.0}));
  REQUIRE(matches(parse_python_complex_string("-1-2j"), {-1.0, -2.0}));
  REQUIRE(matches(parse_python_complex_string("3+j"), {3.0, 1.0}));
  REQUIRE(matches(parse_python_complex_string("3-j"), {3.0, -1.0}));
}

TEST_CASE(
  "parse_python_complex_string: scientific notation must not split on the "
  "exponent sign",
  "[core][python][complex_parser]")
{
  REQUIRE(matches(parse_python_complex_string("1e3+2e-1j"), {1000.0, 0.2}));
  REQUIRE(matches(parse_python_complex_string("1.5E2-3.0E1j"), {150.0, -30.0}));
  REQUIRE(matches(parse_python_complex_string("1e-308"), {1e-308, 0.0}));
}

TEST_CASE(
  "parse_python_complex_string: parentheses and whitespace",
  "[core][python][complex_parser]")
{
  REQUIRE(matches(parse_python_complex_string("(3+4j)"), {3.0, 4.0}));
  REQUIRE(matches(parse_python_complex_string("  5  "), {5.0, 0.0}));
  REQUIRE(matches(parse_python_complex_string(" (3+4j) "), {3.0, 4.0}));
  REQUIRE(matches(parse_python_complex_string("( 3+4j )"), {3.0, 4.0}));
  REQUIRE(matches(parse_python_complex_string("\t-1j\n"), {0.0, -1.0}));
}

TEST_CASE(
  "parse_python_complex_string: rejects malformed input",
  "[core][python][complex_parser]")
{
  REQUIRE(!parse_python_complex_string("").has_value());
  REQUIRE(!parse_python_complex_string("x").has_value());
  REQUIRE(!parse_python_complex_string("1 + 2j").has_value());
  REQUIRE(!parse_python_complex_string("++1j").has_value());
  REQUIRE(!parse_python_complex_string("3+").has_value());
  REQUIRE(!parse_python_complex_string("3+xj").has_value());
  REQUIRE(!parse_python_complex_string("(3+4j").has_value());
  REQUIRE(!parse_python_complex_string("3+4j)").has_value());
  REQUIRE(!parse_python_complex_string("()").has_value());
  REQUIRE(!parse_python_complex_string("+").has_value());
  REQUIRE(!parse_python_complex_string("-").has_value());
}

TEST_CASE(
  "parse_python_complex_string: special values",
  "[core][python][complex_parser]")
{
  // strtod (and CPython) accept "inf", "infinity", "nan" case-insensitively
  // for the real and imag coefficients.
  auto r = parse_python_complex_string("inf");
  REQUIRE(r.has_value());
  REQUIRE(std::isinf(r->first));
  REQUIRE(r->first > 0);
  REQUIRE(r->second == 0.0);

  r = parse_python_complex_string("-inf");
  REQUIRE(r.has_value());
  REQUIRE(std::isinf(r->first));
  REQUIRE(r->first < 0);

  r = parse_python_complex_string("infj");
  REQUIRE(r.has_value());
  REQUIRE(r->first == 0.0);
  REQUIRE(std::isinf(r->second));
  REQUIRE(r->second > 0);

  r = parse_python_complex_string("nan");
  REQUIRE(r.has_value());
  REQUIRE(std::isnan(r->first));
  REQUIRE(r->second == 0.0);
}

TEST_CASE(
  "parse_python_complex_string: does not throw on any input",
  "[core][python][complex_parser]")
{
  // The whole point of replacing std::stod with strtod was to make this
  // function noexcept-in-practice. Sweep a few inputs that std::stod
  // would have thrown on.
  for(auto s :
      {"+", "-", "x", "1.5e", "++1", "--1", "1e1000000", "1e-100000000", ""})
    REQUIRE_NOTHROW(parse_python_complex_string(s));
}
