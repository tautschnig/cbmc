// Spike B: native Go lexer (Go1.27 §Lexical_elements).
// Standalone (no CBMC dependencies) to measure effort and correctness.
#ifndef GO_SPIKE_LEXER_H
#define GO_SPIKE_LEXER_H

#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

enum class tokt
{
  END,
  IDENT,
  INT,
  FLOAT,
  IMAG,
  CHAR,
  STRING,
  OP,     // operators and punctuation; text holds the spelling
  KEYWORD // text holds the keyword
};

struct tokent
{
  tokt kind;
  std::string text;
  int line;
  int col;
  bool is(tokt k, const char *t) const
  {
    return kind == k && text == t;
  }
  bool op(const char *t) const
  {
    return is(tokt::OP, t);
  }
  bool kw(const char *t) const
  {
    return is(tokt::KEYWORD, t);
  }
};

struct syntax_errort : std::runtime_error
{
  int line, col;
  syntax_errort(const std::string &m, int l, int c)
    : std::runtime_error(m), line(l), col(c)
  {
  }
};

/// Tokenise a whole source file, including automatic semicolon insertion
/// (Go1.27 §Semicolons). Inserted semicolons are ordinary ";" OP tokens.
std::vector<tokent> go_lex(const std::string &src);

#endif
