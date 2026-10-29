/// \file
/// Parser for shell arithmetic expressions (POSIX.1-2024 XCU 2.6.4).
///
/// XCU 2.6.4 evaluates the text of $((...)) only after parameter
/// expansion, command substitution, and quote removal, so the parser is
/// applied to text rather than to a pre-built syntax tree. Parameter
/// expansions that were not yet expanded ($x, ${x}, $1, ...) are
/// accepted as primary expressions; the caller expands them.
///
/// The operators are those of XCU 1.1.2.1 "Arithmetic Precision and
/// Operations": the ISO C operators except the comma operator. Prefix
/// and postfix ++ and -- are optional in POSIX, and shells differ (see
/// shell_arith_increment_modet); the comma operator is a common
/// extension (see shell_arith_optionst).

#ifndef CPROVER_SHELL_SHELL_ARITH_H
#define CPROVER_SHELL_SHELL_ARITH_H

#include <util/mp_arith.h>

#include <optional>
#include <string>
#include <vector>

/// An arithmetic expression syntax tree.
struct shell_arith_exprt
{
  enum class kindt
  {
    /// An integer constant; \ref value.
    CONSTANT,
    /// A shell variable referenced by name, as in $((x + 1)); \ref name.
    VARIABLE,
    /// An unexpanded parameter expansion, as in $(($x + 1)); \ref name
    /// is the parameter (a name, a digit string, or one of @*#?-$!).
    PARAMETER,
    /// A prefix operator (+ - ~ ! ++ --); operands[0].
    UNARY,
    /// A postfix operator (++ --); operands[0].
    POSTFIX,
    /// A binary operator, including the comma operator ",";
    /// operands[0] op operands[1].
    BINARY,
    /// operands[0] ? operands[1] : operands[2].
    CONDITIONAL,
    /// \ref name op operands[0], with op one of = *= /= %= += -= <<= >>=
    /// &= ^= |=.
    ASSIGNMENT
  };

  kindt kind = kindt::CONSTANT;
  std::string op;
  mp_integer value = 0;
  std::string name;
  std::vector<shell_arith_exprt> operands;
  /// Byte offset of the expression in the parsed text.
  std::size_t offset = 0;
};

/// How prefix and postfix ++ and -- are treated. POSIX does not require
/// them (XCU 2.6.4), and real shells disagree.
enum class shell_arith_increment_modet
{
  /// No increment operators, as in dash: "--x" is -(-x), and "x--" is a
  /// syntax error.
  NONE,
  /// Increment operators that require a variable operand, as in ksh93,
  /// mksh, yash, posh, and zsh: "--x" decrements, "3--2" is an error.
  STRICT,
  /// Increment operators with a fallback to two sign operators when the
  /// operand is not a variable, as in bash and busybox: "3--2" is 5.
  LENIENT
};

/// Dialect-dependent arithmetic syntax.
struct shell_arith_optionst
{
  shell_arith_increment_modet increment_mode =
    shell_arith_increment_modet::NONE;
  /// Accept the comma operator, which XCU 1.1.2.1 explicitly excludes.
  /// bash, mksh, zsh, ksh93, posh, and busybox accept it; dash and yash
  /// do not.
  bool comma_operator = false;
  /// Read constants with a leading 0 as octal, as ISO C and POSIX do.
  /// mksh and zsh read them as decimal: $((010)) is 10.
  bool leading_zero_octal = true;
};

/// Result of \ref parse_shell_arithmetic.
struct shell_arith_parse_resultt
{
  /// The syntax tree, or nothing on error.
  std::optional<shell_arith_exprt> expression;
  /// The error message if \ref expression is empty.
  std::string error;
  /// True if the text contains "++" or "--" in a position where shells
  /// disagree on its meaning. Set in mode NONE, where callers may want
  /// to warn about non-portable scripts.
  bool contains_increment_sequence = false;
};

/// Parse \p text as a shell arithmetic expression.
shell_arith_parse_resultt parse_shell_arithmetic(
  const std::string &text,
  const shell_arith_optionst &options);

/// Fully parenthesised rendering of \p expr, for tests and debugging.
std::string to_string(const shell_arith_exprt &expr);

#endif // CPROVER_SHELL_SHELL_ARITH_H
