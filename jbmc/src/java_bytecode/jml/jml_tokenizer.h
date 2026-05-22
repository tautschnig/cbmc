/*******************************************************************\

Module: JML Expression Tokenizer

Author: Kiro (AI agent)

\*******************************************************************/

/// \file
/// Tokenizer for JML (Java Modeling Language) expressions.
/// Produces tokens from a JML expression string for consumption
/// by the recursive-descent parser in jml_parser.

#ifndef CPROVER_JAVA_BYTECODE_JML_JML_TOKENIZER_H
#define CPROVER_JAVA_BYTECODE_JML_JML_TOKENIZER_H

#include <string>
#include <vector>

/// Token types for JML expressions.
enum class jml_token_kindt
{
  // Literals
  INTEGER_LITERAL,   // 42, 0xFF, etc.
  BOOLEAN_LITERAL,   // true, false
  NULL_LITERAL,      // null
  STRING_LITERAL,    // "..."

  // Identifiers and keywords
  IDENTIFIER,        // x, myField, etc.
  JML_RESULT,        // \result
  JML_OLD,           // \old
  JML_FORALL,        // \forall
  JML_EXISTS,        // \exists
  JML_FRESH,         // \fresh
  JML_TYPEOF,        // \typeof
  JML_TYPE,          // \type
  JML_NONNULLELEMENTS, // \nonnullelements
  JML_NOTHING,       // \nothing
  JML_EVERYTHING,    // \everything

  // Operators
  PLUS,              // +
  MINUS,             // -
  STAR,              // *
  SLASH,             // /
  PERCENT,           // %
  AMPERSAND,         // &
  PIPE,              // |
  CARET,             // ^
  TILDE,             // ~
  BANG,              // !
  LT,               // <
  GT,               // >
  LE,               // <=
  GE,               // >=
  EQ,               // ==
  NE,               // !=
  AND,              // &&
  OR,               // ||
  IMPLIES,          // ==>
  EQUIV,            // <==>
  NOT_EQUIV,        // <=!=>
  QUESTION,         // ?
  COLON,            // :
  ASSIGN,           // =

  // Delimiters
  LPAREN,           // (
  RPAREN,           // )
  LBRACKET,         // [
  RBRACKET,         // ]
  DOT,              // .
  COMMA,            // ,
  SEMICOLON,        // ;

  // Java keywords used in JML expressions
  INSTANCEOF,       // instanceof
  NEW,              // new
  THIS,             // this
  SUPER,            // super

  // Type keywords
  INT,              // int
  LONG,             // long
  BOOLEAN,          // boolean
  BYTE,             // byte
  SHORT,            // short
  CHAR,             // char
  FLOAT,            // float
  DOUBLE,           // double
  VOID,             // void

  // End of input
  END_OF_INPUT,

  // Error
  ERROR
};

/// A single token from a JML expression.
struct jml_tokent
{
  jml_token_kindt kind;
  std::string text;
  std::size_t position; // byte offset in the source string

  jml_tokent(jml_token_kindt k, std::string t, std::size_t pos)
    : kind{k}, text{std::move(t)}, position{pos}
  {
  }
};

/// Tokenize a JML expression string into a sequence of tokens.
/// \param input: The JML expression text (without surrounding
///   annotation markers like /*@ or @*/).
/// \return A vector of tokens, always ending with END_OF_INPUT.
std::vector<jml_tokent> jml_tokenize(const std::string &input);

#endif // CPROVER_JAVA_BYTECODE_JML_JML_TOKENIZER_H
