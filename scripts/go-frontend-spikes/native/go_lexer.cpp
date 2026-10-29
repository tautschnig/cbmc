// Spike B: native Go lexer (Go1.27 §Lexical_elements).
#include "go_lexer.h"

#include <cctype>
#include <set>

static const std::set<std::string> keywords = {
  "break",  "case",        "chan", "const",   "continue", "default", "defer",
  "else",   "fallthrough", "for",  "func",    "go",       "goto",    "if",
  "import", "interface",   "map",  "package", "range",    "return",  "select",
  "struct", "switch",      "type", "var"};

// Longest-match-first operator table (Go1.27 §Operators_and_punctuation).
static const char *operators[] = {
  "<<=", ">>=", "&^=", "...", "&&", "||", "<-", "++", "--", "==", "!=", "<=",
  ">=",  ":=",  "+=",  "-=",  "*=", "/=", "%=", "&=", "|=", "^=", "<<", ">>",
  "&^",  "+",   "-",   "*",   "/",  "%",  "&",  "|",  "^",  "<",  ">",  "=",
  "!",   "(",   ")",   "[",   "]",  "{",  "}",  ",",  ";",  ".",  ":",  "~"};

namespace
{
struct lexert
{
  const std::string &s;
  std::size_t i = 0;
  int line = 1, col = 1;
  std::vector<tokent> out;

  explicit lexert(const std::string &src) : s(src)
  {
  }

  [[noreturn]] void fail(const std::string &m)
  {
    throw syntax_errort("lex: " + m, line, col);
  }
  char peek(std::size_t k = 0) const
  {
    return i + k < s.size() ? s[i + k] : '\0';
  }
  void adv(std::size_t n = 1)
  {
    while(n-- && i < s.size())
    {
      if(s[i] == '\n')
        line++, col = 1;
      else
        col++;
      i++;
    }
  }

  // Go1.27 §Semicolons rule 1: insert after these final tokens.
  bool needs_semicolon() const
  {
    if(out.empty())
      return false;
    const tokent &t = out.back();
    switch(t.kind)
    {
    case tokt::IDENT:
    case tokt::INT:
    case tokt::FLOAT:
    case tokt::IMAG:
    case tokt::CHAR:
    case tokt::STRING:
      return true;
    case tokt::KEYWORD:
      return t.text == "break" || t.text == "continue" ||
             t.text == "fallthrough" || t.text == "return";
    case tokt::OP:
      return t.text == "++" || t.text == "--" || t.text == ")" ||
             t.text == "]" || t.text == "}";
    default:
      return false;
    }
  }
  void newline()
  {
    if(needs_semicolon())
      out.push_back({tokt::OP, ";", line, col});
  }

  static bool is_letter(unsigned char c)
  {
    return std::isalpha(c) || c == '_' || c >= 0x80; // non-ASCII: letter
  }
  static bool is_digit_in(char c, int base)
  {
    if(base == 16)
      return std::isxdigit(static_cast<unsigned char>(c));
    return c >= '0' && c < '0' + base;
  }

  void number()
  {
    std::size_t start = i;
    int l = line, c = col;
    tokt kind = tokt::INT;
    int base = 10;
    if(peek() == '0' && (peek(1) == 'x' || peek(1) == 'X'))
      base = 16, adv(2);
    else if(peek() == '0' && (peek(1) == 'b' || peek(1) == 'B'))
      base = 2, adv(2);
    else if(peek() == '0' && (peek(1) == 'o' || peek(1) == 'O'))
      base = 8, adv(2);
    auto digits = [&](int b)
    {
      while(is_digit_in(peek(), b == 8 || b == 2 ? 10 : b) || peek() == '_')
        adv();
    };
    digits(base);
    if(peek() == '.' && (base == 10 || base == 16))
      kind = tokt::FLOAT, adv(), digits(base);
    char e = peek();
    if(
      (base == 10 && (e == 'e' || e == 'E')) ||
      (base == 16 && (e == 'p' || e == 'P')))
    {
      kind = tokt::FLOAT;
      adv();
      if(peek() == '+' || peek() == '-')
        adv();
      digits(10);
    }
    if(peek() == 'i')
      kind = tokt::IMAG, adv();
    out.push_back({kind, s.substr(start, i - start), l, c});
  }

  // Shared by rune and interpreted-string literals (Go1.27 §Rune_literals).
  void escape()
  {
    adv(); // backslash
    char c = peek();
    if(std::string("abfnrtv\\'\"").find(c) != std::string::npos)
      return adv();
    int n = c == 'x' ? 2 : c == 'u' ? 4 : c == 'U' ? 8 : 0;
    if(n)
    {
      adv();
      for(int k = 0; k < n; k++, adv())
        if(!std::isxdigit(static_cast<unsigned char>(peek())))
          fail("bad escape");
      return;
    }
    if(c >= '0' && c <= '7')
      return adv(3);
    fail("unknown escape");
  }

  void quoted(char q, tokt kind)
  {
    std::size_t start = i;
    int l = line, c = col;
    adv();
    while(peek() != q)
    {
      if(peek() == '\0' || peek() == '\n')
        fail("unterminated literal");
      if(peek() == '\\')
        escape();
      else
        adv();
    }
    adv();
    out.push_back({kind, s.substr(start, i - start), l, c});
  }

  void run()
  {
    while(i < s.size())
    {
      char c = peek();
      if(c == '\n')
      {
        newline();
        adv();
      }
      else if(c == ' ' || c == '\t' || c == '\r')
        adv();
      else if(c == '/' && peek(1) == '/')
      {
        while(peek() != '\n' && peek() != '\0')
          adv();
      }
      else if(c == '/' && peek(1) == '*')
      {
        // A general comment containing newlines acts like a newline.
        bool nl = false;
        adv(2);
        while(!(peek() == '*' && peek(1) == '/'))
        {
          if(peek() == '\0')
            fail("unterminated comment");
          nl |= peek() == '\n';
          adv();
        }
        adv(2);
        if(nl)
          newline();
      }
      else if(is_letter(static_cast<unsigned char>(c)))
      {
        std::size_t start = i;
        int l = line, cc = col;
        while(is_letter(static_cast<unsigned char>(peek())) ||
              std::isdigit(static_cast<unsigned char>(peek())))
          adv();
        std::string w = s.substr(start, i - start);
        out.push_back(
          {keywords.count(w) ? tokt::KEYWORD : tokt::IDENT, w, l, cc});
      }
      else if(
        std::isdigit(static_cast<unsigned char>(c)) ||
        (c == '.' && std::isdigit(static_cast<unsigned char>(peek(1)))))
        number();
      else if(c == '"')
        quoted('"', tokt::STRING);
      else if(c == '\'')
        quoted('\'', tokt::CHAR);
      else if(c == '`')
      {
        std::size_t start = i;
        int l = line, cc = col;
        adv();
        while(peek() != '`')
        {
          if(peek() == '\0')
            fail("unterminated raw string");
          adv();
        }
        adv();
        out.push_back({tokt::STRING, s.substr(start, i - start), l, cc});
      }
      else
      {
        bool found = false;
        for(const char *op : operators)
        {
          std::size_t n = std::char_traits<char>::length(op);
          if(s.compare(i, n, op) == 0)
          {
            out.push_back({tokt::OP, op, line, col});
            adv(n);
            found = true;
            break;
          }
        }
        if(!found)
          fail(std::string("unexpected character '") + c + "'");
      }
    }
    newline();
    out.push_back({tokt::END, "", line, col});
  }
};
} // namespace

std::vector<tokent> go_lex(const std::string &src)
{
  lexert l(src);
  l.run();
  return std::move(l.out);
}
