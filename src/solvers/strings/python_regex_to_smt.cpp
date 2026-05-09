/*******************************************************************\

Module: Translate a Python regular-expression pattern to an SMT-LIB
        2.6 regex term.

Author: Wave 2 of Python re support.

\*******************************************************************/

#include "python_regex_to_smt.h"

#include <cctype>
#include <sstream>
#include <vector>

namespace
{
/// Hand-written recursive-descent translator from a Python
/// regex string to an SMT-LIB 2.6 regex term.
class translator
{
public:
  translator(const std::string &p) : pattern(p)
  {
  }

  /// Top-level parse entry. Returns the SMT regex term for the
  /// whole pattern, or nullopt on unsupported input. Does not
  /// add any outer anchoring — callers that need fullmatch /
  /// match / search semantics wrap the result appropriately.
  std::optional<std::string> parse_top()
  {
    auto alt = parse_alternation();
    if(!alt.has_value())
      return std::nullopt;
    if(pos != pattern.size())
      return std::nullopt; // trailing junk
    return alt;
  }

private:
  const std::string &pattern;
  std::size_t pos = 0;

  // -- low-level helpers --

  bool eof() const
  {
    return pos >= pattern.size();
  }

  char peek() const
  {
    return pattern[pos];
  }

  bool accept(char c)
  {
    if(!eof() && peek() == c)
    {
      ++pos;
      return true;
    }
    return false;
  }

  /// Return the SMT-LIB string literal for a single character.
  static std::string smt_char_lit(unsigned char c)
  {
    std::ostringstream out;
    // Printable ASCII that isn't a quote or backslash can appear
    // literally; otherwise use \u{hex}.
    if(c == '"' || c == '\\' || c < 0x20 || c > 0x7e)
      out << "(_ char #x" << std::hex;
    if(c >= 0x10)
      out << std::hex;
    else
      out << "0" << std::hex;
    if(!(c == '"' || c == '\\' || c < 0x20 || c > 0x7e))
    {
      out.str("");
      out.clear();
      out << "\"" << static_cast<char>(c) << "\"";
      return out.str();
    }
    out.str("");
    out.clear();
    out << "(_ char #x" << std::hex;
    if(c < 0x10)
      out << "0";
    out << static_cast<unsigned>(c) << ")";
    return out.str();
  }

  /// Return the SMT-LIB regex that matches the single character c.
  static std::string smt_str_to_re_char(unsigned char c)
  {
    // SMT-LIB 2.6 string theory: (str.to_re "x") is the regex
    // accepting exactly the one-character string "x".
    std::ostringstream out;
    if(c >= 0x20 && c <= 0x7e && c != '"' && c != '\\')
    {
      out << "(str.to_re \"" << static_cast<char>(c) << "\")";
    }
    else if(c == '"')
    {
      out << "(str.to_re \"\\\"\\\"\")"; // SMT-LIB: "" escapes "
    }
    else if(c == '\\')
    {
      out << "(str.to_re \"\\\\\")"; // Actually SMT-LIB strings
                                     // don't need backslash escapes
                                     // for \, but CVC5 accepts it.
    }
    else
    {
      // Non-printable: use re.range over [c, c].
      out << "(re.range (_ char #x";
      if(c < 0x10)
        out << "0";
      out << std::hex << static_cast<unsigned>(c) << ") (_ char #x";
      if(c < 0x10)
        out << "0";
      out << std::hex << static_cast<unsigned>(c) << "))";
    }
    return out.str();
  }

  // -- grammar --

  /// alternation := concat ( '|' concat )*
  std::optional<std::string> parse_alternation()
  {
    auto first = parse_concat();
    if(!first.has_value())
      return std::nullopt;
    if(eof() || peek() != '|')
      return first;
    std::vector<std::string> alts;
    alts.push_back(*first);
    while(accept('|'))
    {
      auto next = parse_concat();
      if(!next.has_value())
        return std::nullopt;
      alts.push_back(*next);
    }
    std::ostringstream out;
    out << "(re.union";
    for(const auto &a : alts)
      out << " " << a;
    out << ")";
    return out.str();
  }

  /// concat := quantified ( quantified )*
  std::optional<std::string> parse_concat()
  {
    std::vector<std::string> items;
    while(!eof() && peek() != '|' && peek() != ')')
    {
      auto q = parse_quantified();
      if(!q.has_value())
        return std::nullopt;
      items.push_back(*q);
    }
    if(items.empty())
    {
      // Empty concat matches the empty string.
      return std::string{"(str.to_re \"\")"};
    }
    if(items.size() == 1)
      return items[0];
    std::ostringstream out;
    out << "(re.++";
    for(const auto &i : items)
      out << " " << i;
    out << ")";
    return out.str();
  }

  /// quantified := atom ( '*' | '+' | '?' | '{m,n}' )?
  std::optional<std::string> parse_quantified()
  {
    auto atom = parse_atom();
    if(!atom.has_value())
      return std::nullopt;
    if(eof())
      return atom;

    char q = peek();
    if(q == '*')
    {
      ++pos;
      // Possessive / non-greedy variants — consume an optional '?'.
      if(!eof() && peek() == '?')
        ++pos;
      return std::string{"(re.* "} + *atom + ")";
    }
    if(q == '+')
    {
      ++pos;
      if(!eof() && peek() == '?')
        ++pos;
      return std::string{"(re.+ "} + *atom + ")";
    }
    if(q == '?')
    {
      ++pos;
      if(!eof() && peek() == '?')
        ++pos;
      return std::string{"(re.opt "} + *atom + ")";
    }
    if(q == '{')
    {
      // {m} or {m,} or {m,n}
      std::size_t save = pos;
      ++pos;
      std::string num;
      while(!eof() && std::isdigit(static_cast<unsigned char>(peek())))
      {
        num += peek();
        ++pos;
      }
      if(num.empty())
      {
        pos = save;
        return atom; // not a quantifier after all — '{' is literal
      }
      int m = std::stoi(num);
      int n = m;
      bool has_upper = true;
      if(accept(','))
      {
        std::string num2;
        while(!eof() && std::isdigit(static_cast<unsigned char>(peek())))
        {
          num2 += peek();
          ++pos;
        }
        if(num2.empty())
          has_upper = false;
        else
          n = std::stoi(num2);
      }
      if(!accept('}'))
      {
        pos = save;
        return atom;
      }
      if(!eof() && peek() == '?')
        ++pos; // non-greedy
      std::ostringstream out;
      if(has_upper)
        out << "((_ re.loop " << m << " " << n << ") " << *atom << ")";
      else
      {
        // {m,} — at least m repetitions.
        out << "(re.++ ((_ re.loop " << m << " " << m << ") " << *atom
            << ") (re.* " << *atom << "))";
      }
      return out.str();
    }
    return atom;
  }

  /// atom := literal | '.' | '\\X' | '[' class ']' | '(' group ')'
  ///       | anchor (^,$ — handled at top level)
  std::optional<std::string> parse_atom()
  {
    if(eof())
      return std::nullopt;
    char c = peek();

    if(c == '(')
    {
      ++pos;
      // Non-capturing group "(?:..." support.
      if(!eof() && peek() == '?')
      {
        ++pos;
        if(!eof() && (peek() == ':'))
        {
          ++pos;
        }
        else
        {
          // Lookahead (?=), lookbehind (?<=), named group (?P<name>)
          // and other (?...) forms aren't supported.
          return std::nullopt;
        }
      }
      auto inner = parse_alternation();
      if(!inner.has_value())
        return std::nullopt;
      if(!accept(')'))
        return std::nullopt;
      return inner;
    }

    if(c == '[')
    {
      ++pos;
      return parse_class();
    }

    if(c == '.')
    {
      ++pos;
      // Dot matches any character except a line break by default,
      // but we don't distinguish line-break handling — use any
      // character.
      return std::string{"re.allchar"};
    }

    if(c == '^' || c == '$')
    {
      // Anchors are handled by the outer fullmatch/match/search
      // wrappers; if they appear inside the pattern they're
      // position constraints we can't express. Reject.
      return std::nullopt;
    }

    if(c == '\\')
    {
      ++pos;
      if(eof())
        return std::nullopt;
      return parse_escape();
    }

    // Literal single character.
    ++pos;
    return smt_str_to_re_char(static_cast<unsigned char>(c));
  }

  /// \X escape — character classes and literal escapes.
  std::optional<std::string> parse_escape()
  {
    char c = peek();
    ++pos;
    switch(c)
    {
    case 'd':
      return std::string{"(re.range \"0\" \"9\")"};
    case 'D':
      return std::string{"(re.comp (re.range \"0\" \"9\"))"};
    case 's':
      // Whitespace: [ \t\n\r\f\v]
      return std::string{
        "(re.union (str.to_re \" \") (str.to_re \"\\t\") "
        "(str.to_re \"\\n\") (str.to_re \"\\r\") "
        "(str.to_re \"\\f\") (str.to_re \"\\v\"))"};
    case 'S':
      return std::string{
        "(re.comp (re.union (str.to_re \" \") (str.to_re \"\\t\") "
        "(str.to_re \"\\n\") (str.to_re \"\\r\") "
        "(str.to_re \"\\f\") (str.to_re \"\\v\")))"};
    case 'w':
      // Word: [A-Za-z0-9_]
      return std::string{
        "(re.union (re.range \"A\" \"Z\") (re.range \"a\" \"z\") "
        "(re.range \"0\" \"9\") (str.to_re \"_\"))"};
    case 'W':
      return std::string{
        "(re.comp (re.union (re.range \"A\" \"Z\") "
        "(re.range \"a\" \"z\") (re.range \"0\" \"9\") "
        "(str.to_re \"_\")))"};
    case 'n':
      return std::string{"(str.to_re \"\\n\")"};
    case 't':
      return std::string{"(str.to_re \"\\t\")"};
    case 'r':
      return std::string{"(str.to_re \"\\r\")"};
    case 'b':
    case 'B':
      // Word boundaries are position assertions — unsupported.
      return std::nullopt;
    default:
      // Backreferences \1..\9
      if(std::isdigit(static_cast<unsigned char>(c)))
        return std::nullopt;
      // Literal escape (e.g. \. \* \+): just the character itself.
      return smt_str_to_re_char(static_cast<unsigned char>(c));
    }
  }

  /// Parse '[...]'. Pos is just past the opening '['.
  std::optional<std::string> parse_class()
  {
    if(eof())
      return std::nullopt;
    bool negated = accept('^');
    std::vector<std::string> parts;
    while(!eof() && peek() != ']')
    {
      char c = peek();
      ++pos;
      std::string lhs;
      if(c == '\\')
      {
        // Escape — re-use parse_escape() by stashing pos. But
        // parse_escape expects pos to point past the backslash;
        // we already consumed it.
        auto e = parse_escape();
        if(!e.has_value())
          return std::nullopt;
        lhs = *e;
      }
      else
      {
        lhs = smt_str_to_re_char(static_cast<unsigned char>(c));
      }
      // Range a-b?
      if(
        !eof() && peek() == '-' && pos + 1 < pattern.size() &&
        pattern[pos + 1] != ']')
      {
        ++pos; // consume -
        char d = peek();
        ++pos;
        if(d == '\\')
          // Range with escaped RHS (\t, \n...): not supported for
          // simplicity.
          return std::nullopt;
        std::ostringstream out;
        out << "(re.range ";
        if(c >= 0x20 && c <= 0x7e && c != '"' && c != '\\')
          out << "\"" << c << "\"";
        else
          return std::nullopt;
        out << " ";
        if(d >= 0x20 && d <= 0x7e && d != '"' && d != '\\')
          out << "\"" << d << "\"";
        else
          return std::nullopt;
        out << ")";
        parts.push_back(out.str());
      }
      else
      {
        parts.push_back(lhs);
      }
    }
    if(!accept(']'))
      return std::nullopt;
    std::ostringstream out;
    if(parts.empty())
      return std::nullopt;
    if(parts.size() == 1)
    {
      if(negated)
        return std::string{"(re.comp "} + parts[0] + ")";
      return parts[0];
    }
    out << "(re.union";
    for(const auto &p : parts)
      out << " " << p;
    out << ")";
    if(negated)
      return std::string{"(re.comp "} + out.str() + ")";
    return out.str();
  }
};

} // namespace

std::optional<std::string>
python_regex_to_smt_fullmatch(const std::string &pattern)
{
  // Handle leading/trailing anchors specially. Python anchors
  // inside a fullmatch are redundant but not errors; we tolerate
  // leading ^ and trailing $.
  std::string core = pattern;
  if(!core.empty() && core.front() == '^')
    core.erase(core.begin());
  if(!core.empty() && core.back() == '$')
    core.pop_back();
  translator t{core};
  return t.parse_top();
}

std::optional<std::string> python_regex_to_smt_match(const std::string &pattern)
{
  auto body = python_regex_to_smt_fullmatch(pattern);
  if(!body.has_value())
    return std::nullopt;
  // match(pattern, s) is anchored at the start but not the end;
  // allow arbitrary suffix.
  return std::string{"(re.++ "} + *body + " (re.* re.allchar))";
}

std::optional<std::string>
python_regex_to_smt_search(const std::string &pattern)
{
  auto body = python_regex_to_smt_fullmatch(pattern);
  if(!body.has_value())
    return std::nullopt;
  // search(pattern, s): match anywhere.
  return std::string{"(re.++ (re.* re.allchar) "} + *body +
         " (re.* re.allchar))";
}
