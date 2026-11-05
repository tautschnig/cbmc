/// \file
/// Internal representation (IR) of parsed shell programs.
///
/// The IR is deliberately independent of any particular parser: the
/// shfmt JSON reader (shfmt_reader.h) produces it today, and a native
/// parser can produce it later. It follows the structure of the POSIX
/// Shell Command Language, POSIX.1-2024 XCU chapter 2, and keeps
/// exactly the information that later semantic stages need. In
/// particular, words keep their quoting structure, because field
/// splitting (XCU 2.6.5) and pathname expansion (XCU 2.6.6) depend on
/// which parts of a word were quoted.

#ifndef CPROVER_SHELL_SHELL_AST_H
#define CPROVER_SHELL_SHELL_AST_H

#include <util/source_location.h>

#include "shell_dialect.h"

#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

struct shell_commandt;

/// A list of commands, as in a POSIX `compound_list`.
using shell_command_listt = std::vector<shell_commandt>;

/// One part of a word (XCU 2.2 Quoting, 2.6 Word Expansions).
struct shell_word_partt
{
  enum class kindt
  {
    /// Unquoted text, subject to field splitting and pathname
    /// expansion. Backslash escapes (XCU 2.2.1) are still present.
    LITERAL,
    /// Text whose quoting removes all special meaning: '...' (XCU 2.2.2)
    /// and the decoded contents of $'...' (XCU 2.2.4).
    QUOTED_LITERAL,
    /// "..." (XCU 2.2.3); the contents are in \ref parts.
    DOUBLE_QUOTED,
    /// Parameter expansion $name or ${...} (XCU 2.6.2).
    PARAMETER,
    /// Command substitution $(...) or `...` (XCU 2.6.3).
    COMMAND_SUBSTITUTION,
    /// Arithmetic expansion $((...)) (XCU 2.6.4).
    ARITHMETIC
  };

  kindt kind = kindt::LITERAL;

  /// LITERAL / QUOTED_LITERAL: the text. PARAMETER: the parameter
  /// name. ARITHMETIC: the raw source text between "$((" and "))".
  /// XCU 2.6.4 evaluates this text only after parameter expansion,
  /// command substitution, and quote removal, so it is kept unparsed.
  std::string text;

  /// DOUBLE_QUOTED: the contents. PARAMETER: the word operand of a
  /// ${parameter<op>word} expansion.
  std::vector<shell_word_partt> parts;

  /// PARAMETER: one of "", ":-", "-", ":=", "=", ":?", "?", ":+", "+",
  /// "%", "%%", "#", "##" (XCU 2.6.2).
  std::string parameter_operator;

  /// PARAMETER: ${#parameter}, the string length (XCU 2.6.2).
  bool parameter_length = false;

  /// PARAMETER: the subscript of ${name[subscript]} (bash, mksh): "@",
  /// "*", or an arithmetic expression; empty without one.
  std::vector<shell_word_partt> index;

  /// PARAMETER with the operators "/", "//", "/#" and "/%" of
  /// ${parameter/pattern/string} (bash, mksh, zsh): the string, whose
  /// pattern is in \ref parts.
  std::vector<shell_word_partt> replacement;

  /// COMMAND_SUBSTITUTION: the command list. A shared pointer, as
  /// shell_commandt is incomplete here.
  std::shared_ptr<shell_command_listt> commands;

  source_locationt location;
};

/// A word: a sequence of parts that are concatenated before field
/// splitting (XCU 2.6).
using shell_wordt = std::vector<shell_word_partt>;

/// An I/O redirection (XCU 2.7).
struct shell_redirectt
{
  /// The file descriptor given before the operator, or -1 for the
  /// operator's default (0 for input, 1 for output).
  int fd = -1;

  /// One of "<", ">", ">|", ">>", "<&", ">&", "<>", "<<", "<<-".
  std::string op;

  /// The operand word. For here-documents, the delimiter.
  shell_wordt target;

  /// Here-document body (XCU 2.7.4). If the delimiter was quoted, the
  /// body is a single QUOTED_LITERAL part; otherwise it may contain
  /// expansions.
  shell_wordt here_document;

  source_locationt location;
};

/// A variable assignment in a simple command (XCU 2.9.1).
struct shell_assignmentt
{
  std::string name;
  shell_wordt value;
  /// name+=value (bash, mksh, zsh): append to the value or the array.
  bool append = false;
  /// name=(elements) (bash, mksh, zsh): an array of these words.
  bool is_array = false;
  std::vector<shell_wordt> elements;
  /// name[index]=value: the subscript, an arithmetic expression; empty
  /// without one.
  shell_wordt index;
  source_locationt location;
};

/// One item of a case conditional construct (XCU 2.9.4.3).
struct shell_case_itemt
{
  std::vector<shell_wordt> patterns;
  shell_command_listt body;
  /// ";;" or ";&" (the latter is new in POSIX.1-2024).
  std::string terminator;
  source_locationt location;
};

/// An expression of a [[ ... ]] test (bash, mksh, zsh).
struct shell_testt
{
  enum class kindt
  {
    /// A word on its own: whether it is not empty.
    WORD,
    /// op word, as in -n word; ! operands[0].
    UNARY,
    /// word op word, as in a == b; operands[0] && operands[1], and ||.
    BINARY
  };
  kindt kind = kindt::WORD;
  std::string op;
  /// WORD and UNARY other than !: the operand; BINARY other than && and
  /// ||: the left operand.
  shell_wordt word;
  /// BINARY other than && and ||: the right operand.
  shell_wordt right;
  /// !, && and ||: the operands.
  std::vector<shell_testt> operands;
  source_locationt location;
};

/// A command (XCU 2.9). Which members are used depends on \ref kind,
/// as documented on each member.
struct shell_commandt
{
  enum class kindt
  {
    /// Simple command (XCU 2.9.1): assignments, words, redirects.
    SIMPLE,
    /// Pipeline (XCU 2.9.2) of two or more commands in \ref body.
    PIPELINE,
    /// AND list `a && b` (XCU 2.9.3.3): body[0] && body[1].
    AND,
    /// OR list `a || b` (XCU 2.9.3.4): body[0] || body[1].
    OR,
    /// ( list ) (XCU 2.9.4.1), executed in a subshell environment.
    SUBSHELL,
    /// { list; } (XCU 2.9.4.1), executed in the current environment.
    BRACE_GROUP,
    /// if (XCU 2.9.4.4): condition, body, else_body. An `elif` is an
    /// IF that is the only command in else_body.
    IF,
    /// while (XCU 2.9.4.5): condition, body.
    WHILE,
    /// until (XCU 2.9.4.6): condition, body.
    UNTIL,
    /// for (XCU 2.9.4.2): name, for_items (if has_in), body.
    FOR,
    /// case (XCU 2.9.4.3): case_word, case_items.
    CASE,
    /// Function definition (XCU 2.9.5): name, body (one command).
    FUNCTION_DEFINITION
  };

  kindt kind = kindt::SIMPLE;
  source_locationt location;

  /// ! pipeline (XCU 2.9.2): negate the exit status.
  bool negated = false;
  /// Terminated by & (XCU 2.9.3.1): run asynchronously.
  bool background = false;
  /// Redirections applied to this command.
  std::vector<shell_redirectt> redirects;

  /// SIMPLE only.
  std::vector<shell_assignmentt> assignments;
  /// SIMPLE: the command name and arguments.
  std::vector<shell_wordt> words;
  /// SIMPLE: for [[ ... ]], the expression, whose words are also the
  /// arguments (so that what looks at words sees them).
  std::vector<shell_testt> test;

  /// The main command list of compound commands; the elements of a
  /// PIPELINE; the two operands of AND/OR.
  shell_command_listt body;
  /// IF, WHILE, UNTIL: the condition list.
  shell_command_listt condition;
  /// IF: the else (or elif) branch.
  shell_command_listt else_body;

  /// FOR: the loop variable. FUNCTION_DEFINITION: the function name.
  std::string name;
  /// FOR: whether an `in` clause is present; without one the loop
  /// iterates over the positional parameters ("$@").
  bool has_in = false;
  /// FOR: the words after `in`.
  std::vector<shell_wordt> for_items;

  /// CASE: the word being matched.
  shell_wordt case_word;
  /// CASE: the items.
  std::vector<shell_case_itemt> case_items;
};

/// A complete program: the top-level command list of a script.
struct shell_programt
{
  shell_command_listt commands;
  std::string filename;
  shell_dialectt dialect = shell_dialectt::POSIX;
  /// Single-letter options on the #! line, as in "#!/bin/sh -eu".
  std::string initial_options;

  /// Print the IR as an indented tree, one node per line. This is the
  /// output of --show-parse-tree and is used by regression tests, so
  /// keep it stable.
  void output(std::ostream &out) const;
};

#endif // CPROVER_SHELL_SHELL_AST_H
