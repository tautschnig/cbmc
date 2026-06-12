/*******************************************************************\

Module: COBOL source scanner

Author: Kiro

\*******************************************************************/

/// \file
/// Tokeniser for the COBOL frontend.

#include "cobol_scanner.h"

#include <cctype>
#include <istream>

/// Returns true if \p c may appear inside a COBOL word (letter, digit, or an
/// internal hyphen, which the caller handles specially).
static bool is_word_char(char c)
{
  return std::isalnum(static_cast<unsigned char>(c)) != 0;
}

/// Given that `s[i]` is a digit, decide whether the maximal word-run starting
/// there is a COBOL word (identifier / paragraph name such as `1000-MAIN`)
/// rather than a numeric literal. It is a word if the run contains a letter or
/// an internal hyphen.
static bool digit_run_is_word(const std::string &s, std::size_t i)
{
  const std::size_t n = s.size();
  std::size_t j = i;
  while(j < n)
  {
    if(std::isalpha(static_cast<unsigned char>(s[j])) != 0)
      return true;
    if(std::isdigit(static_cast<unsigned char>(s[j])) != 0)
    {
      ++j;
      continue;
    }
    if(s[j] == '-' && j + 1 < n && is_word_char(s[j + 1]))
      return true;
    break;
  }
  return false;
}

/// Extract the code portion of one physical source line, honouring fixed
/// format. Columns 1-6 are the sequence area, column 7 is the indicator
/// (`*` and `/` start a comment, `-` is continuation which we treat as a
/// plain continuation by ignoring the indicator), columns 8-72 are code.
/// Lines shorter than the indicator column are returned as-is so that blank
/// lines and very short free-format lines still work.
/// \param line: the raw physical line (without newline)
/// \param [out] is_comment: set to true if the whole line is a comment
/// \return the code text of the line
static std::string extract_code(const std::string &line, bool &is_comment)
{
  is_comment = false;

  // Short lines: nothing in the indicator/code area.
  if(line.size() <= 6)
    return std::string{};

  const char indicator = line[6];
  if(indicator == '*' || indicator == '/')
  {
    is_comment = true;
    return std::string{};
  }

  // Code area is columns 8-72 (0-indexed 7..71). Tolerate lines that run
  // past column 72 by clipping.
  const std::size_t start = 7;
  const std::size_t end = std::min<std::size_t>(line.size(), 72);
  if(end <= start)
    return std::string{};
  return line.substr(start, end - start);
}

std::vector<cobol_tokent>
cobol_scan(std::istream &in, const std::string &file_name)
{
  std::vector<cobol_tokent> tokens;

  std::string raw_line;
  std::size_t line_no = 0;

  while(std::getline(in, raw_line))
  {
    ++line_no;

    // Strip a trailing carriage return (Windows line endings).
    if(!raw_line.empty() && raw_line.back() == '\r')
      raw_line.pop_back();

    bool is_comment = false;
    const std::string code = extract_code(raw_line, is_comment);
    if(is_comment)
      continue;

    const auto make_location = [&](std::size_t col)
    {
      source_locationt loc;
      loc.set_file(file_name);
      loc.set_line(std::to_string(line_no));
      loc.set_column(std::to_string(col + 1));
      return loc;
    };

    std::size_t i = 0;
    const std::size_t n = code.size();
    while(i < n)
    {
      const char c = code[i];

      // Whitespace.
      if(std::isspace(static_cast<unsigned char>(c)) != 0)
      {
        ++i;
        continue;
      }

      cobol_tokent token;
      token.location = make_location(i + 7);

      // Quoted string literal: "..." or '...'.
      if(c == '"' || c == '\'')
      {
        const char quote = c;
        ++i;
        std::string text;
        while(i < n)
        {
          if(code[i] == quote)
          {
            // A doubled quote is an escaped quote.
            if(i + 1 < n && code[i + 1] == quote)
            {
              text.push_back(quote);
              i += 2;
              continue;
            }
            ++i;
            break;
          }
          text.push_back(code[i]);
          ++i;
        }
        token.kind = cobol_token_kindt::STRING;
        token.text = text;
        tokens.push_back(token);
        continue;
      }

      // Numeric literal: a run of digits, with an optional internal decimal
      // point (only when followed by a further digit so the sentence period
      // is not consumed). A digit-led run containing a letter or hyphen is a
      // COBOL word (e.g. the paragraph name 1000-MAIN), handled below.
      if(
        std::isdigit(static_cast<unsigned char>(c)) != 0 &&
        !digit_run_is_word(code, i))
      {
        std::string text;
        while(i < n)
        {
          if(std::isdigit(static_cast<unsigned char>(code[i])) != 0)
          {
            text.push_back(code[i]);
            ++i;
          }
          else if(
            code[i] == '.' && i + 1 < n &&
            std::isdigit(static_cast<unsigned char>(code[i + 1])) != 0 &&
            text.find('.') == std::string::npos)
          {
            text.push_back('.');
            ++i;
          }
          else
            break;
        }
        token.kind = cobol_token_kindt::NUMBER;
        token.text = text;
        tokens.push_back(token);
        continue;
      }

      // Word: letters/digits with internal single hyphens.
      if(is_word_char(c))
      {
        std::string text;
        while(i < n)
        {
          if(is_word_char(code[i]))
          {
            text.push_back(code[i]);
            ++i;
          }
          else if(code[i] == '-' && i + 1 < n && is_word_char(code[i + 1]))
          {
            text.push_back('-');
            ++i;
          }
          else
            break;
        }
        // Upper-case for case-insensitive reserved-word matching.
        for(char &ch : text)
          ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        token.kind = cobol_token_kindt::WORD;
        token.text = text;
        tokens.push_back(token);
        continue;
      }

      // Punctuation and operators.
      switch(c)
      {
      case '.':
        token.kind = cobol_token_kindt::PERIOD;
        token.text = ".";
        ++i;
        break;
      case '(':
        token.kind = cobol_token_kindt::LPAREN;
        token.text = "(";
        ++i;
        break;
      case ')':
        token.kind = cobol_token_kindt::RPAREN;
        token.text = ")";
        ++i;
        break;
      case ',':
      case ';':
        // COBOL treats comma and semicolon as optional separators; drop them.
        ++i;
        continue;
      case '>':
      case '<':
        token.kind = cobol_token_kindt::PUNCT;
        if(i + 1 < n && code[i + 1] == '=')
        {
          token.text = std::string{c} + "=";
          i += 2;
        }
        else if(c == '<' && i + 1 < n && code[i + 1] == '>')
        {
          token.text = "<>";
          i += 2;
        }
        else
        {
          token.text = std::string{c};
          ++i;
        }
        break;
      case '=':
      case '+':
      case '*':
      case '/':
        token.kind = cobol_token_kindt::PUNCT;
        token.text = std::string{c};
        ++i;
        break;
      case '-':
        token.kind = cobol_token_kindt::PUNCT;
        token.text = "-";
        ++i;
        break;
      default:
        // Unknown character: skip it.
        ++i;
        continue;
      }
      tokens.push_back(token);
    }
  }

  cobol_tokent eof;
  eof.kind = cobol_token_kindt::END_OF_FILE;
  eof.text = "";
  source_locationt loc;
  loc.set_file(file_name);
  eof.location = loc;
  tokens.push_back(eof);

  return tokens;
}
