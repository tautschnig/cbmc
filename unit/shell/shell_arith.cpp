/// \file
/// Unit tests for the shell arithmetic expression parser.

#include <shell/shell_arith.h>
#include <testing-utils/use_catch.h>

namespace
{
const shell_arith_optionst posix{
  shell_arith_increment_modet::NONE,
  false,
  true};
const shell_arith_optionst bash{
  shell_arith_increment_modet::LENIENT,
  true,
  true};
const shell_arith_optionst mksh{
  shell_arith_increment_modet::STRICT,
  true,
  false};

/// The fully parenthesised rendering, or "error: ..." on failure.
std::string parse(const std::string &text, const shell_arith_optionst &options)
{
  const auto result = parse_shell_arithmetic(text, options);
  if(!result.expression.has_value())
    return "error: " + result.error;
  return to_string(*result.expression);
}

bool is_error(const std::string &text, const shell_arith_optionst &options)
{
  return !parse_shell_arithmetic(text, options).expression.has_value();
}
} // namespace

TEST_CASE("shell arithmetic: C precedence and associativity", "[shell]")
{
  REQUIRE(parse("1 + 2 * 3", posix) == "(1 + (2 * 3))");
  REQUIRE(parse("1 - 2 - 3", posix) == "((1 - 2) - 3)");
  REQUIRE(parse("(1 + 2) * 3", posix) == "((1 + 2) * 3)");
  REQUIRE(parse("8 / 4 % 3", posix) == "((8 / 4) % 3)");
  REQUIRE(parse("1 << 2 < 3", posix) == "((1 << 2) < 3)");
  REQUIRE(parse("1 < 2 == 3 >= 4", posix) == "((1 < 2) == (3 >= 4))");
  REQUIRE(parse("1 | 2 ^ 3 & 4", posix) == "(1 | (2 ^ (3 & 4)))");
  REQUIRE(parse("1 || 2 && 3", posix) == "(1 || (2 && 3))");
  REQUIRE(parse("!a && ~b", posix) == "((!a) && (~b))");
  REQUIRE(parse("-+1", posix) == "(-(+1))");
}

TEST_CASE("shell arithmetic: assignment and conditional", "[shell]")
{
  REQUIRE(parse("x = y = 1", posix) == "(x = (y = 1))");
  REQUIRE(parse("x <<= 2", posix) == "(x <<= 2)");
  REQUIRE(parse("x |= y ^= 1", posix) == "(x |= (y ^= 1))");
  REQUIRE(parse("a ? b : c ? d : e", posix) == "(a ? b : (c ? d : e))");
  // Shells accept an assignment as the third operand.
  REQUIRE(parse("c ? x = 1 : x = 2", posix) == "(c ? (x = 1) : (x = 2))");
  REQUIRE(is_error("1 = 2", posix));
  REQUIRE(is_error("(x) + 1 = 2", posix));
  // Comparison operators are not assignments.
  REQUIRE(parse("x == 1", posix) == "(x == 1)");
  REQUIRE(parse("x != 1", posix) == "(x != 1)");
  REQUIRE(parse("x <= 1", posix) == "(x <= 1)");
}

TEST_CASE("shell arithmetic: constants", "[shell]")
{
  REQUIRE(parse("42", posix) == "42");
  REQUIRE(parse("010", posix) == "8");
  REQUIRE(parse("0x1f", posix) == "31");
  REQUIRE(parse("0X1F", posix) == "31");
  REQUIRE(parse("0", posix) == "0");
  // Larger than any machine integer: kept exactly.
  REQUIRE(
    parse("123456789012345678901234567890", posix) ==
    "123456789012345678901234567890");
  REQUIRE(is_error("08", posix));
  // mksh and zsh read a leading 0 as decimal.
  REQUIRE(parse("010", mksh) == "10");
  REQUIRE(parse("08", mksh) == "8");
  REQUIRE(parse("0x10", mksh) == "16");
  REQUIRE(is_error("0x", posix));
  REQUIRE(is_error("12abc", posix));
}

TEST_CASE("shell arithmetic: variables and parameters", "[shell]")
{
  REQUIRE(parse("x + _y1", posix) == "(x + _y1)");
  REQUIRE(parse("$x + ${y}", posix) == "($x + $y)");
  REQUIRE(parse("$1 + ${10} + $#", posix) == "(($1 + $10) + $#)");
  REQUIRE(is_error("${x:-1}", posix));
  REQUIRE(is_error("$", posix));
}

TEST_CASE("shell arithmetic: syntax errors", "[shell]")
{
  REQUIRE(is_error("", posix));
  REQUIRE(is_error("   ", posix));
  REQUIRE(is_error("1 +", posix));
  REQUIRE(is_error("(1", posix));
  REQUIRE(is_error("1 2", posix));
  REQUIRE(is_error("a ? b", posix));
}

TEST_CASE("shell arithmetic: comma operator is a dialect option", "[shell]")
{
  // XCU 1.1.2.1 excludes the comma operator; dash rejects it.
  REQUIRE(is_error("a, b", posix));
  REQUIRE(parse("a = 1, b", bash) == "((a = 1) , b)");
  REQUIRE(parse("1, 2, 3", mksh) == "((1 , 2) , 3)");
  REQUIRE(parse("(1, 2) + 3", bash) == "((1 , 2) + 3)");
  // As in C, the middle operand of ?: is an expression.
  REQUIRE(parse("c ? 1, 2 : 3", bash) == "(c ? (1 , 2) : 3)");
}

// The expected results are those of dash 0.5.12 (NONE), bash 5.2 and
// busybox (LENIENT), and mksh 59c, zsh 5.9, ksh93u+m, yash, and posh
// (STRICT).
TEST_CASE("shell arithmetic: increment operators", "[shell]")
{
  SECTION("NONE: two sign operators, as in dash")
  {
    const auto result = parse_shell_arithmetic("--x", posix);
    REQUIRE(result.expression.has_value());
    REQUIRE(to_string(*result.expression) == "(-(-x))");
    REQUIRE(result.contains_increment_sequence);
    REQUIRE(parse("3--2", posix) == "(3 - (-2))");
    REQUIRE(parse("++3", posix) == "(+(+3))");
    REQUIRE(is_error("x++", posix));
    REQUIRE_FALSE(
      parse_shell_arithmetic("3 - -2", posix).contains_increment_sequence);
  }

  SECTION("LENIENT: fall back to sign operators, as in bash")
  {
    REQUIRE(parse("--x", bash) == "(--x)");
    REQUIRE(parse("x++", bash) == "(x++)");
    REQUIRE(parse("-- 3", bash) == "(-(-3))");
    REQUIRE(parse("3--2", bash) == "(3 - (-2))");
    REQUIRE(parse("x++ + ++y", bash) == "((x++) + (++y))");
    // "a--b" is a postfix decrement followed by a stray operand.
    REQUIRE(is_error("a--b", bash));
    REQUIRE_FALSE(
      parse_shell_arithmetic("--x", bash).contains_increment_sequence);
  }

  SECTION("STRICT: a variable operand is required, as in mksh")
  {
    REQUIRE(parse("--x", mksh) == "(--x)");
    REQUIRE(parse("x--", mksh) == "(x--)");
    REQUIRE(is_error("-- 3", mksh));
    REQUIRE(is_error("3--2", mksh));
    REQUIRE(is_error("a--b", mksh));
    REQUIRE(parse("3 - -2", mksh) == "(3 - (-2))");
  }
}
