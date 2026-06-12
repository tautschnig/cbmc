/*******************************************************************\

Module: COBOL source scanner

Author: Kiro

\*******************************************************************/

/// \file
/// Tokeniser for the COBOL frontend.

#include "cobol_scanner.h"

#include <util/message.h>

#include <cctype>
#include <fstream>
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

        // A PICTURE character-string is a single token "delimited only by the
        // separator space" (IBM Enterprise COBOL for z/OS 6.4 Language
        // Reference, "PICTURE character-strings", pp. 48 / 3723-3725). Read it
        // verbatim so edit characters (. , - + $ Z * B 0 / CR DB) are not
        // mistaken for separators (in particular, an embedded '.' is not the
        // sentence period).
        if(text == "PIC" || text == "PICTURE")
        {
          while(i < n && std::isspace(static_cast<unsigned char>(code[i])) != 0)
            ++i;
          // optional "IS"
          if(
            i + 1 < n &&
            std::toupper(static_cast<unsigned char>(code[i])) == 'I' &&
            std::toupper(static_cast<unsigned char>(code[i + 1])) == 'S' &&
            (i + 2 >= n ||
             std::isspace(static_cast<unsigned char>(code[i + 2])) != 0))
          {
            i += 2;
            while(i < n &&
                  std::isspace(static_cast<unsigned char>(code[i])) != 0)
              ++i;
          }
          if(i < n && std::isspace(static_cast<unsigned char>(code[i])) == 0)
          {
            std::string pic;
            while(i < n &&
                  std::isspace(static_cast<unsigned char>(code[i])) == 0)
            {
              pic.push_back(code[i]);
              ++i;
            }
            // A trailing '.' is the sentence/clause separator period, not part
            // of the picture.
            bool trailing_period = false;
            if(!pic.empty() && pic.back() == '.')
            {
              pic.pop_back();
              trailing_period = true;
            }
            for(char &ch : pic)
              ch =
                static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            cobol_tokent pic_token;
            pic_token.kind = cobol_token_kindt::WORD;
            pic_token.text = pic;
            pic_token.location = token.location;
            tokens.push_back(pic_token);
            if(trailing_period)
            {
              cobol_tokent period;
              period.kind = cobol_token_kindt::PERIOD;
              period.text = ".";
              period.location = token.location;
              tokens.push_back(period);
            }
          }
        }
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

// ---------------------------------------------------------------------------
// COPY statement expansion
//
// IBM Enterprise COBOL for z/OS 6.4 Language Reference, "COPY statement",
// pp. 688-697. "The effect of processing a COPY statement is that the library
// text associated with text-name is copied into the compilation unit,
// logically replacing the entire COPY statement, beginning with the word COPY
// and ending with the period, inclusive." (p. 688)
// ---------------------------------------------------------------------------

namespace
{
bool tok_is_word(const cobol_tokent &t, const char *w)
{
  return t.kind == cobol_token_kindt::WORD && t.text == w;
}

bool tok_is_eq(const cobol_tokent &t)
{
  return t.kind == cobol_token_kindt::PUNCT && t.text == "=";
}

bool tokens_equal(const cobol_tokent &a, const cobol_tokent &b)
{
  return a.kind == b.kind && a.text == b.text;
}

/// Read a REPLACING operand starting at \p pos: either a pseudo-text run
/// bounded by `==` ... `==` (LR p. 690) or a single text word / literal.
/// Advances \p pos past the operand and returns its token sequence.
std::vector<cobol_tokent>
read_replacing_operand(const std::vector<cobol_tokent> &t, std::size_t &pos)
{
  std::vector<cobol_tokent> operand;
  // Pseudo-text: two consecutive '=' tokens open and close the run.
  if(pos + 1 < t.size() && tok_is_eq(t[pos]) && tok_is_eq(t[pos + 1]))
  {
    pos += 2; // opening ==
    while(pos + 1 < t.size() && !(tok_is_eq(t[pos]) && tok_is_eq(t[pos + 1])))
      operand.push_back(t[pos++]);
    if(pos + 1 < t.size())
      pos += 2; // closing ==
    return operand;
  }
  // Single word / literal operand.
  if(t[pos].kind != cobol_token_kindt::END_OF_FILE)
    operand.push_back(t[pos++]);
  return operand;
}

/// Apply REPLACING pairs to a copybook token sequence (LR p. 690). Each
/// occurrence of a pattern subsequence is replaced by its replacement; pairs
/// are tried left-to-right at each position.
std::vector<cobol_tokent> apply_replacing(
  const std::vector<cobol_tokent> &in,
  const std::vector<
    std::pair<std::vector<cobol_tokent>, std::vector<cobol_tokent>>> &pairs)
{
  if(pairs.empty())
    return in;

  std::vector<cobol_tokent> out;
  std::size_t i = 0;
  while(i < in.size())
  {
    bool matched = false;
    for(const auto &pair : pairs)
    {
      const auto &pat = pair.first;
      if(pat.empty() || i + pat.size() > in.size())
        continue;
      bool eq = true;
      for(std::size_t k = 0; k < pat.size(); ++k)
        if(!tokens_equal(in[i + k], pat[k]))
        {
          eq = false;
          break;
        }
      if(eq)
      {
        for(const auto &r : pair.second)
          out.push_back(r);
        i += pat.size();
        matched = true;
        break;
      }
    }
    if(!matched)
      out.push_back(in[i++]);
  }
  return out;
}

/// Resolve a copybook name to a path. Tries each search directory with the
/// common copybook extensions. text-name is matched case-insensitively via the
/// extensions; the name itself is used as given (LR p. 688: 1-30 chars,
/// A-Z/a-z/0-9/hyphen).
std::string
resolve_copybook(const std::string &name, const std::vector<std::string> &dirs)
{
  static const char *exts[] = {".cpy", ".CPY", ".cbl", ".CBL", ""};
  for(const std::string &dir : dirs)
  {
    const std::string base = dir.empty() ? name : dir + "/" + name;
    for(const char *ext : exts)
    {
      const std::string candidate = base + ext;
      std::ifstream probe(candidate);
      if(probe.good())
        return candidate;
    }
  }
  return std::string{};
}
} // namespace

std::vector<cobol_tokent> cobol_expand_copy(
  std::vector<cobol_tokent> tokens,
  const std::vector<std::string> &copybook_dirs,
  message_handlert &message_handler)
{
  // Bound recursion to guard against cyclic copybooks.
  static thread_local std::size_t depth = 0;
  struct depth_guardt
  {
    std::size_t &d;
    explicit depth_guardt(std::size_t &d) : d(d)
    {
      ++d;
    }
    ~depth_guardt()
    {
      --d;
    }
  } guard{depth};

  messaget log{message_handler};

  std::vector<cobol_tokent> out;
  std::size_t i = 0;
  while(i < tokens.size())
  {
    if(tokens[i].kind == cobol_token_kindt::END_OF_FILE)
    {
      out.push_back(tokens[i]);
      break;
    }

    if(!tok_is_word(tokens[i], "COPY"))
    {
      out.push_back(tokens[i++]);
      continue;
    }

    // COPY directive.
    const source_locationt copy_loc = tokens[i].location;
    ++i; // consume COPY

    if(
      i >= tokens.size() || (tokens[i].kind != cobol_token_kindt::WORD &&
                             tokens[i].kind != cobol_token_kindt::STRING))
    {
      log.error() << "COBOL: COPY without a text-name" << messaget::eom;
      // Skip to the terminating period to recover.
      while(i < tokens.size() && tokens[i].kind != cobol_token_kindt::PERIOD &&
            tokens[i].kind != cobol_token_kindt::END_OF_FILE)
        ++i;
      if(i < tokens.size() && tokens[i].kind == cobol_token_kindt::PERIOD)
        ++i;
      continue;
    }

    const std::string name = tokens[i].text;
    ++i;

    // Optional OF/IN library-name (LR p. 688).
    if(
      i < tokens.size() &&
      (tok_is_word(tokens[i], "OF") || tok_is_word(tokens[i], "IN")))
    {
      i += 2; // OF/IN and the library-name
    }
    // Optional SUPPRESS phrase.
    if(i < tokens.size() && tok_is_word(tokens[i], "SUPPRESS"))
      ++i;

    // Optional REPLACING phrase (LR p. 690).
    std::vector<std::pair<std::vector<cobol_tokent>, std::vector<cobol_tokent>>>
      replacing;
    if(i < tokens.size() && tok_is_word(tokens[i], "REPLACING"))
    {
      ++i;
      while(i < tokens.size() && tokens[i].kind != cobol_token_kindt::PERIOD &&
            tokens[i].kind != cobol_token_kindt::END_OF_FILE)
      {
        // LEADING / TRAILING partial-word replacement is not modelled; skip
        // the keyword and fall through to read the operands.
        if(
          tok_is_word(tokens[i], "LEADING") ||
          tok_is_word(tokens[i], "TRAILING"))
          ++i;
        std::vector<cobol_tokent> pattern = read_replacing_operand(tokens, i);
        if(i < tokens.size() && tok_is_word(tokens[i], "BY"))
          ++i;
        std::vector<cobol_tokent> replacement =
          read_replacing_operand(tokens, i);
        replacing.emplace_back(std::move(pattern), std::move(replacement));
      }
    }

    // Consume the terminating period (it is part of the COPY statement and is
    // not emitted; LR p. 688).
    if(i < tokens.size() && tokens[i].kind == cobol_token_kindt::PERIOD)
      ++i;

    // Resolve and splice the copybook.
    const std::string path = resolve_copybook(name, copybook_dirs);
    if(path.empty())
    {
      log.warning().source_location = copy_loc;
      log.warning() << "COBOL: copybook '" << name
                    << "' not found; skipping COPY" << messaget::eom;
      continue;
    }
    if(depth > 40)
    {
      log.error() << "COBOL: COPY nesting too deep (cyclic copybook '" << name
                  << "'?)" << messaget::eom;
      continue;
    }

    std::ifstream in{path};
    std::vector<cobol_tokent> sub =
      cobol_expand_copy(cobol_scan(in, path), copybook_dirs, message_handler);
    // Drop the copybook's trailing END_OF_FILE before splicing.
    if(!sub.empty() && sub.back().kind == cobol_token_kindt::END_OF_FILE)
      sub.pop_back();
    sub = apply_replacing(sub, replacing);
    for(auto &tok : sub)
      out.push_back(std::move(tok));
  }

  return out;
}
