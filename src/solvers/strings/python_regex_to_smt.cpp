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
      // Python rejects {m,n} with n < m ("min repeat greater than max
      // repeat"); bail so the caller models it as nondet rather than
      // emitting a degenerate re.loop.
      if(has_upper && n < m)
        return std::nullopt;
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
      // Python '.' (without re.DOTALL) matches any character EXCEPT a
      // newline. SMT 're.allchar' includes '\n', so subtract it; using
      // 're.allchar' here would be unsound (over-matching across lines).
      // SMT-LIB encodes the newline code point as \u{a} ("\n" would be a
      // literal backslash-n).
      return std::string{"(re.diff re.allchar (str.to_re \"\\u{a}\"))"};
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
      // Single non-digit character: allchar minus the digit range.
      // (re.comp ...) would be unsound — its language includes "" and
      // multi-character strings, but \D matches exactly one character.
      return std::string{"(re.diff re.allchar (re.range \"0\" \"9\"))"};
    case 's':
      // Whitespace: [ \t\n\r\f\v]. Control characters use \u{hex}
      // (SMT-LIB has no \t / \n escapes — "\t" would be backslash-t).
      return std::string{
        "(re.union (str.to_re \" \") (str.to_re \"\\u{9}\") "
        "(str.to_re \"\\u{a}\") (str.to_re \"\\u{d}\") "
        "(str.to_re \"\\u{c}\") (str.to_re \"\\u{b}\"))"};
    case 'S':
      return std::string{
        "(re.diff re.allchar (re.union (str.to_re \" \") "
        "(str.to_re \"\\u{9}\") (str.to_re \"\\u{a}\") (str.to_re \"\\u{d}\") "
        "(str.to_re \"\\u{c}\") (str.to_re \"\\u{b}\")))"};
    case 'w':
      // Word: [A-Za-z0-9_]
      return std::string{
        "(re.union (re.range \"A\" \"Z\") (re.range \"a\" \"z\") "
        "(re.range \"0\" \"9\") (str.to_re \"_\"))"};
    case 'W':
      return std::string{
        "(re.diff re.allchar (re.union (re.range \"A\" \"Z\") "
        "(re.range \"a\" \"z\") (re.range \"0\" \"9\") "
        "(str.to_re \"_\")))"};
    case 'n':
      return std::string{"(str.to_re \"\\u{a}\")"};
    case 't':
      return std::string{"(str.to_re \"\\u{9}\")"};
    case 'r':
      return std::string{"(str.to_re \"\\u{d}\")"};
    case 'f':
      return std::string{"(str.to_re \"\\u{c}\")"};
    case 'v':
      return std::string{"(str.to_re \"\\u{b}\")"};
    case 'b':
    case 'B':
    case 'A':
    case 'Z':
      // Word boundaries (\b, \B) and string-position anchors (\A, \Z)
      // are zero-width position assertions we can't express as a regex
      // term — bail so the caller falls back to a sound nondet model.
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
    // A negated class [^...] matches exactly one character NOT in the
    // set (newline included, per Python). Use allchar-minus-set rather
    // than (re.comp set): the bare complement's language also contains
    // "" and multi-character strings, which would be unsound.
    if(parts.size() == 1)
    {
      if(negated)
        return std::string{"(re.diff re.allchar "} + parts[0] + ")";
      return parts[0];
    }
    out << "(re.union";
    for(const auto &p : parts)
      out << " " << p;
    out << ")";
    if(negated)
      return std::string{"(re.diff re.allchar "} + out.str() + ")";
    return out.str();
  }
};

/// Parallel parser that determines whether a Python regex
/// pattern's language contains the empty string. Mirrors the
/// SMT translator's grammar; returns std::nullopt for the same
/// unsupported features (back-references, lookaround, named
/// captures), which the caller treats as "don't know".
///
/// The implementation walks the same surface syntax but
/// computes a bool per node instead of an SMT term:
///
///   alt(a, b, ...)  : OR over children
///   concat(a, b, ...): AND over children
///   a*, a?          : true
///   a+              : a accepts ε
///   a{m,n}, a{m,}   : m == 0 || a accepts ε
///   group(a)        : a accepts ε
///   literal char    : false
///   '.'             : false (matches exactly one char)
///   char class      : false (matches exactly one char)
///   ^, $            : true (anchors don't consume input)
class empty_acceptance_checker
{
public:
  empty_acceptance_checker(const std::string &p) : pattern(p)
  {
  }

  std::optional<bool> parse_top()
  {
    auto alt = parse_alternation();
    if(!alt.has_value())
      return std::nullopt;
    if(pos != pattern.size())
      return std::nullopt;
    return alt;
  }

private:
  const std::string &pattern;
  std::size_t pos = 0;

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

  /// alternation := concat ('|' concat)*  →  OR
  std::optional<bool> parse_alternation()
  {
    auto first = parse_concat();
    if(!first.has_value())
      return std::nullopt;
    bool acc = *first;
    while(accept('|'))
    {
      auto next = parse_concat();
      if(!next.has_value())
        return std::nullopt;
      acc = acc || *next;
    }
    return acc;
  }

  /// concat := quantified+   →  AND (empty concat → true)
  std::optional<bool> parse_concat()
  {
    bool acc = true;
    bool any = false;
    while(!eof() && peek() != '|' && peek() != ')')
    {
      auto q = parse_quantified();
      if(!q.has_value())
        return std::nullopt;
      acc = acc && *q;
      any = true;
    }
    (void)any; // empty concat is fine; acc stays true
    return acc;
  }

  /// quantified := atom (* | + | ? | {m,n})?
  std::optional<bool> parse_quantified()
  {
    auto atom = parse_atom();
    if(!atom.has_value())
      return std::nullopt;
    if(eof())
      return atom;
    char q = peek();
    if(q == '*' || q == '?')
    {
      ++pos;
      if(!eof() && peek() == '?')
        ++pos; // non-greedy
      return true;
    }
    if(q == '+')
    {
      ++pos;
      if(!eof() && peek() == '?')
        ++pos;
      // a+ accepts ε iff a does (at least one repetition required).
      return atom;
    }
    if(q == '{')
    {
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
        return atom;
      }
      int m = std::stoi(num);
      int n = m;
      (void)n;
      if(accept(','))
      {
        std::string num2;
        while(!eof() && std::isdigit(static_cast<unsigned char>(peek())))
        {
          num2 += peek();
          ++pos;
        }
        if(!num2.empty())
          n = std::stoi(num2);
      }
      if(!accept('}'))
      {
        pos = save;
        return atom;
      }
      if(!eof() && peek() == '?')
        ++pos; // non-greedy
      // a{m,n} accepts ε iff m == 0 || a accepts ε.
      // (m == 0 makes 0 repetitions a valid choice. has_upper or
      // not doesn't matter for ε-acceptance.)
      if(m == 0)
        return true;
      return atom;
    }
    return atom;
  }

  /// atom := group | class | escape | '.' | anchor | literal
  std::optional<bool> parse_atom()
  {
    if(eof())
      return std::nullopt;
    char c = peek();
    if(c == '(')
    {
      ++pos;
      if(!eof() && peek() == '?')
      {
        ++pos;
        if(!eof() && peek() == ':')
          ++pos; // non-capturing group
        else
          return std::nullopt; // (?=... lookaround etc.
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
      // Skip past the class — content doesn't matter for
      // ε-acceptance (a single class always consumes one char).
      bool negated = false;
      if(!eof() && peek() == '^')
      {
        negated = true;
        ++pos;
      }
      (void)negated;
      // First ']' immediately after '[' or '[^' is treated as
      // a literal ']' character per Python regex rules. Match
      // the existing translator's handling: just scan to ']'.
      bool first = true;
      while(!eof() && (first || peek() != ']'))
      {
        if(peek() == '\\' && pos + 1 < pattern.size())
          pos += 2;
        else
          ++pos;
        first = false;
      }
      if(!accept(']'))
        return std::nullopt;
      return false; // single class consumes a char
    }
    if(c == '.')
    {
      ++pos;
      return false;
    }
    if(c == '^' || c == '$')
    {
      // The wrappers (search/match/fullmatch) strip leading ^ and
      // trailing $ before calling. If we still see one inside the
      // body, it's a position constraint we can't handle.
      return std::nullopt;
    }
    if(c == '\\')
    {
      ++pos;
      if(eof())
        return std::nullopt;
      char e = peek();
      ++pos;
      // Most escapes consume one character; reject unsupported
      // ones that appear in the existing translator's reject list.
      // Back-references (\1..\9, \g<name>) → unsupported.
      if(std::isdigit(static_cast<unsigned char>(e)))
        return std::nullopt;
      if(e == 'g')
        return std::nullopt;
      if(e == 'b' || e == 'B' || e == 'A' || e == 'Z')
      {
        // Word/string boundaries don't consume a character but
        // are position constraints we don't otherwise model.
        // Conservatively treat as ε-accepting for the purposes
        // of this check (don't fire the no-match assertion when
        // a boundary is present). This is sound: we'd rather
        // miss a bug than report a false positive.
        return true;
      }
      // Everything else is a single-character match: \d, \D, \s,
      // \S, \w, \W, \n, \t, \r, \f, \v, and literal escapes.
      return false;
    }
    // Literal character.
    ++pos;
    return false;
  }
};

} // namespace

namespace
{
enum class match_kind
{
  fullmatch,
  match,
  search
};

/// Split off a single leading ^ anchor and a single trailing $ anchor,
/// returning the anchor-free core plus flags. A trailing $ that is
/// escaped (\$) is a literal, not an anchor, so it is not stripped.
/// Any ^/$ that remains embedded in the core is rejected by the
/// translator (parse_atom), which keeps the model sound.
void strip_anchors(
  const std::string &pattern,
  bool &had_start,
  bool &had_end,
  std::string &core)
{
  core = pattern;
  had_start = false;
  had_end = false;
  if(!core.empty() && core.front() == '^')
  {
    had_start = true;
    core.erase(core.begin());
  }
  if(!core.empty() && core.back() == '$')
  {
    // Count the run of backslashes immediately before the '$'. An even
    // count (including zero) means the '$' is unescaped → an anchor.
    std::size_t bs = 0;
    std::size_t i = core.size() - 1; // index of '$'
    while(i > 0 && core[i - 1] == '\\')
    {
      ++bs;
      --i;
    }
    if(bs % 2 == 0)
    {
      had_end = true;
      core.pop_back();
    }
  }
}

/// Translate a pattern to the SMT regex for the whole subject string
/// under the given match semantics. Returns nullopt for unsupported
/// patterns (caller falls back to a sound nondet model).
std::optional<std::string>
translate(const std::string &pattern, match_kind kind)
{
  bool had_start, had_end;
  std::string core;
  strip_anchors(pattern, had_start, had_end, core);
  translator t{core};
  auto body = t.parse_top();
  if(!body.has_value())
    return std::nullopt;

  // Effective anchoring combines the function semantics with explicit
  // ^/$ anchors:
  //  - start anchored unless this is search() without a leading ^;
  //  - end anchored for fullmatch (exact, no trailing-newline slack);
  //    for match()/search() a trailing $ anchors the end but, per
  //    Python's non-MULTILINE rule, $ also matches just before a single
  //    trailing newline, so allow an optional final '\n'.
  const bool start_anchored = (kind != match_kind::search) || had_start;

  std::string prefix = start_anchored ? std::string{} : "(re.* re.allchar)";
  std::string suffix;
  if(kind == match_kind::fullmatch)
    suffix = std::string{}; // exact end
  else if(had_end)
    suffix = "(re.opt (str.to_re \"\\u{a}\"))";
  else
    suffix = "(re.* re.allchar)"; // match/search: end unanchored

  std::vector<std::string> parts;
  if(!prefix.empty())
    parts.push_back(prefix);
  parts.push_back(*body);
  if(!suffix.empty())
    parts.push_back(suffix);
  if(parts.size() == 1)
    return parts[0];
  std::ostringstream out;
  out << "(re.++";
  for(const auto &p : parts)
    out << " " << p;
  out << ")";
  return out.str();
}
} // namespace

std::optional<std::string>
python_regex_to_smt_fullmatch(const std::string &pattern)
{
  return translate(pattern, match_kind::fullmatch);
}

std::optional<std::string> python_regex_to_smt_match(const std::string &pattern)
{
  return translate(pattern, match_kind::match);
}

std::optional<std::string>
python_regex_to_smt_search(const std::string &pattern)
{
  return translate(pattern, match_kind::search);
}

std::optional<std::string> python_regex_to_smt_body(const std::string &pattern)
{
  // No anchor stripping and no wrapping: parse_top translates the pattern's
  // language directly and rejects any embedded ^/$ (parse_atom returns
  // nullopt), which is what a positional-context-free caller needs.
  translator t{pattern};
  return t.parse_top();
}

std::optional<bool> python_regex_can_match_empty(const std::string &pattern)
{
  // Strip leading ^ and trailing $ — the wrappers do this for
  // SMT translation. For ε-acceptance, anchors don't change the
  // answer: search/match/fullmatch all reduce to "is the empty
  // string in the language?", and with no input there's no
  // distinction between anchored and unanchored positions.
  std::string core = pattern;
  if(!core.empty() && core.front() == '^')
    core.erase(core.begin());
  if(!core.empty() && core.back() == '$')
    core.pop_back();
  empty_acceptance_checker chk{core};
  return chk.parse_top();
}

namespace
{
/// Parallel parser computing the fixed match length of a Python regex
/// pattern's language, or std::nullopt when the length is variable or the
/// pattern is unsupported. Mirrors the translator/empty-checker grammar but
/// carries an int length per node:
///   alt(a,b,...)    : a length only if all branches share it, else nullopt
///   concat(a,b,...) : sum (empty concat -> 0)
///   a* a+ a?        : variable -> nullopt
///   a{m,n}          : m*len(a) if m==n, else nullopt
///   group(a)        : len(a)
///   literal / . / class / single-char escape : 1
///   ^, $            : nullopt (anchors are not part of a bare regex term)
class fixed_length_checker
{
public:
  fixed_length_checker(const std::string &p) : pattern(p)
  {
  }

  std::optional<int> parse_top()
  {
    auto alt = parse_alternation();
    if(!alt.has_value())
      return std::nullopt;
    if(pos != pattern.size())
      return std::nullopt;
    return alt;
  }

private:
  const std::string &pattern;
  std::size_t pos = 0;

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

  std::optional<int> parse_alternation()
  {
    auto first = parse_concat();
    if(!first.has_value())
      return std::nullopt;
    int len = *first;
    while(accept('|'))
    {
      auto next = parse_concat();
      if(!next.has_value())
        return std::nullopt;
      if(*next != len)
        return std::nullopt; // unequal-length alternatives -> variable
    }
    return len;
  }

  std::optional<int> parse_concat()
  {
    int total = 0;
    while(!eof() && peek() != '|' && peek() != ')')
    {
      auto q = parse_quantified();
      if(!q.has_value())
        return std::nullopt;
      total += *q;
    }
    return total;
  }

  std::optional<int> parse_quantified()
  {
    auto atom = parse_atom();
    if(!atom.has_value())
      return std::nullopt;
    if(eof())
      return atom;
    char q = peek();
    if(q == '*' || q == '+' || q == '?')
    {
      ++pos;
      if(!eof() && peek() == '?')
        ++pos;             // non-greedy marker
      return std::nullopt; // variable length
    }
    if(q == '{')
    {
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
        return atom; // not a quantifier
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
        ++pos;
      if(!has_upper || n != m)
        return std::nullopt; // {m,} or {m,n} with m!=n -> variable
      return m * *atom;
    }
    return atom;
  }

  std::optional<int> parse_atom()
  {
    if(eof())
      return std::nullopt;
    char c = peek();
    if(c == '(')
    {
      ++pos;
      if(!eof() && peek() == '?')
      {
        ++pos;
        if(!eof() && peek() == ':')
          ++pos; // non-capturing group
        else
          return std::nullopt; // lookaround / named group etc.
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
      if(!eof() && peek() == '^')
        ++pos;
      bool first = true;
      while(!eof() && (first || peek() != ']'))
      {
        if(peek() == '\\' && pos + 1 < pattern.size())
          pos += 2;
        else
          ++pos;
        first = false;
      }
      if(!accept(']'))
        return std::nullopt;
      return 1; // a single class consumes exactly one character
    }
    if(c == '.')
    {
      ++pos;
      return 1;
    }
    if(c == '^' || c == '$')
      return std::nullopt; // anchor: not a fixed-width consuming atom
    if(c == '\\')
    {
      ++pos;
      if(eof())
        return std::nullopt;
      char e = peek();
      ++pos;
      if(std::isdigit(static_cast<unsigned char>(e)))
        return std::nullopt; // back-reference
      if(e == 'g')
        return std::nullopt; // named back-reference
      if(e == 'b' || e == 'B' || e == 'A' || e == 'Z')
        return std::nullopt; // zero-width assertion (also rejected upstream)
      return 1;              // \d \D \s \S \w \W \n \t \r \f \v, literal escape
    }
    ++pos;
    return 1; // literal character
  }
};
} // namespace

std::optional<int> python_regex_fixed_length(const std::string &pattern)
{
  fixed_length_checker chk{pattern};
  return chk.parse_top();
}
