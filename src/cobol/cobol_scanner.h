/*******************************************************************\

Module: COBOL source scanner

Author: Kiro

\*******************************************************************/

/// \file
/// Tokeniser for the COBOL frontend.
///
/// Normalises both fixed-format (columns 1-6 sequence area, column 7
/// indicator, columns 8-72 code) and short free-format lines into a flat
/// stream of \ref cobol_tokent values tagged with source locations. The
/// parser (in cobol_typecheck.cpp) consumes this stream.

#ifndef CPROVER_COBOL_COBOL_SCANNER_H
#define CPROVER_COBOL_COBOL_SCANNER_H

#include <util/source_location.h>

#include <iosfwd>
#include <string>
#include <vector>

/// Kinds of lexical token the COBOL scanner produces.
enum class cobol_token_kindt
{
  WORD,       ///< identifier or (reserved) word, e.g. MOVE, WS-A
  NUMBER,     ///< numeric literal, e.g. 5, 0.30, 999999
  STRING,     ///< quoted literal, e.g. "abc" or 'abc'
  PERIOD,     ///< statement/sentence terminator '.'
  LPAREN,     ///< '('
  RPAREN,     ///< ')'
  PUNCT,      ///< operator/relational punctuation: + - * / = > < >= <= <>
  END_OF_FILE ///< sentinel
};

/// A single lexical token.
struct cobol_tokent
{
  cobol_token_kindt kind;
  /// Text of the token. For WORD tokens this is upper-cased so reserved-word
  /// matching is case-insensitive; for STRING it is the unquoted contents.
  std::string text;
  source_locationt location;
};

/// Tokenise a COBOL source stream.
/// \param in: input stream
/// \param file_name: source file name, used for source locations
/// \return the token stream, terminated by an END_OF_FILE token
std::vector<cobol_tokent>
cobol_scan(std::istream &in, const std::string &file_name);

#endif // CPROVER_COBOL_COBOL_SCANNER_H
