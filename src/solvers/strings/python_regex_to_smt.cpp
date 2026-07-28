/*******************************************************************\

Module: Translate a Python regular-expression pattern to an SMT-LIB
        2.6 regex term.

Author: Wave 2 of Python re support.

\*******************************************************************/

#include "python_regex_to_smt.h"

#include <cctype>
#include <functional>
#include <memory>
#include <sstream>
#include <vector>

regex_char_classest python_regex_char_classes()
{
  regex_char_classest c;
  c.digit = "(re.range \"0\" \"9\")";
  // PLR: \s is the Unicode whitespace set; within ASCII (the byte model's
  // exact domain) that is \t \n \v \f \r and \x1c-\x20 (FS GS RS US space --
  // verified against CPython; note \x1c-\x1f, which Java's \s lacks).
  // Code points >= 0x80 are multi-byte in the UTF-8 byte model (documented
  // model boundary).
  c.whitespace =
    "(re.union (re.range \"\\u{9}\" \"\\u{d}\") "
    "(re.range \"\\u{1c}\" \"\\u{20}\"))";
  c.word =
    "(re.union (re.range \"A\" \"Z\") (re.range \"a\" \"z\") "
    "(re.range \"0\" \"9\") (str.to_re \"_\"))";
  // PLR: '.' (without re.DOTALL) matches anything except \n.
  c.dot_excluded = "(str.to_re \"\\u{a}\")";
  return c;
}

regex_char_classest java_regex_char_classes()
{
  regex_char_classest c;
  // java.util.regex.Pattern javadoc (no UNICODE_CHARACTER_CLASS):
  c.digit = "(re.range \"0\" \"9\")";
  // \s = [ \t\n\x0B\f\r] -- does NOT include \x1c-\x1f (differs from both
  // Python's \s and Java's own Character.isWhitespace).
  c.whitespace =
    "(re.union (re.range \"\\u{9}\" \"\\u{d}\") "
    "(str.to_re \" \"))";
  c.word =
    "(re.union (re.range \"A\" \"Z\") (re.range \"a\" \"z\") "
    "(re.range \"0\" \"9\") (str.to_re \"_\"))";
  // '.' (without DOTALL) excludes the Java line terminators
  // \n \r \u0085 \u2028 \u2029 (exact: JBMC chars are UTF-16 code units).
  c.dot_excluded =
    "(re.union (re.range \"\\u{a}\" \"\\u{a}\") (str.to_re \"\\u{d}\") "
    "(str.to_re \"\\u{85}\") (str.to_re \"\\u{2028}\") "
    "(str.to_re \"\\u{2029}\"))";
  return c;
}

namespace
{
/// Hand-written recursive-descent translator from a Python
/// regex string to an SMT-LIB 2.6 regex term.

class translator
{
public:
  translator(
    const std::string &p,
    bool ic = false,
    bool da = false,
    regex_char_classest cc = python_regex_char_classes())
    : pattern(p), ignorecase(ic), dotall(da), classes(std::move(cc))
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
  const bool ignorecase = false;
  const bool dotall = false;
  const regex_char_classest classes;
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

  /// Regex matching the single character ``c``, honouring IGNORECASE: an
  /// ASCII letter matches either case (CPython ASCII case folding).
  std::string re_char(unsigned char c) const
  {
    if(ignorecase && std::isalpha(c))
    {
      const unsigned char lo = static_cast<unsigned char>(std::tolower(c));
      const unsigned char up = static_cast<unsigned char>(std::toupper(c));
      return "(re.union " + smt_str_to_re_char(lo) + " " +
             smt_str_to_re_char(up) + ")";
    }
    return smt_str_to_re_char(c);
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
      // '.' (without DOTALL) matches any character except the dialect's
      // line terminators (Python: \n; Java: \n \r \u0085 \u2028 \u2029).
      // Using bare 're.allchar' would be unsound (over-matching across
      // lines).
      if(dotall)
        return std::string{"re.allchar"};
      return std::string{"(re.diff re.allchar "} + classes.dot_excluded + ")";
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
    return re_char(static_cast<unsigned char>(c));
  }

  /// \X escape — character classes and literal escapes.
  std::optional<std::string> parse_escape()
  {
    char c = peek();
    ++pos;
    switch(c)
    {
    case 'd':
      return classes.digit;
    case 'D':
      // Single non-digit character: allchar minus the digit range.
      // (re.comp ...) would be unsound — its language includes "" and
      // multi-character strings, but \D matches exactly one character.
      return std::string{"(re.diff re.allchar "} + classes.digit + ")";
    case 's':
      // Whitespace: [ \t\n\r\f\v]. Control characters use \u{hex}
      // (SMT-LIB has no \t / \n escapes — "\t" would be backslash-t).
      return classes.whitespace;
    case 'S':
      return std::string{"(re.diff re.allchar "} + classes.whitespace + ")";
    case 'w':
      // Word: [A-Za-z0-9_]
      return classes.word;
    case 'W':
      return std::string{"(re.diff re.allchar "} + classes.word + ")";
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
      return re_char(static_cast<unsigned char>(c));
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
        lhs = re_char(static_cast<unsigned char>(c));
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
        if(
          !(c >= 0x20 && c <= 0x7e && c != '"' && c != '\\') ||
          !(d >= 0x20 && d <= 0x7e && d != '"' && d != '\\'))
          return std::nullopt;
        auto emit_range = [](char lo, char hi)
        {
          std::ostringstream r;
          r << "(re.range \"" << lo << "\" \"" << hi << "\")";
          return r.str();
        };
        std::string range_re = emit_range(c, d);
        if(ignorecase)
        {
          const bool c_lower = c >= 'a' && c <= 'z';
          const bool d_lower = d >= 'a' && d <= 'z';
          const bool c_upper = c >= 'A' && c <= 'Z';
          const bool d_upper = d >= 'A' && d <= 'Z';
          // Letters occupy [A-Z] (65..90) and [a-z] (97..122).
          const bool hits_letters =
            !((static_cast<unsigned char>(d) < 'A') ||
              (static_cast<unsigned char>(c) > 'z') ||
              (static_cast<unsigned char>(c) > 'Z' &&
               static_cast<unsigned char>(d) < 'a'));
          if(c_lower && d_lower)
            range_re = "(re.union " + range_re + " " +
                       emit_range(
                         static_cast<char>(std::toupper(c)),
                         static_cast<char>(std::toupper(d))) +
                       ")";
          else if(c_upper && d_upper)
            range_re = "(re.union " + range_re + " " +
                       emit_range(
                         static_cast<char>(std::tolower(c)),
                         static_cast<char>(std::tolower(d))) +
                       ")";
          else if(hits_letters)
            // A range overlapping the letters but not a clean single-case
            // letter range (e.g. [A-z], [0-z]) can't be folded simply; bail.
            return std::nullopt;
        }
        parts.push_back(range_re);
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

/// Strip a leading global inline-flag group — e.g. ``(?i)``, ``(?s)``,
/// ``(?is)`` — setting ``ignorecase`` / ``dotall`` and returning the rest of
/// the pattern. Returns ``std::nullopt`` if the group contains a flag letter
/// we don't model (a/L/m/u/x), so the caller falls back to a sound nondet
/// model. A pattern with no leading flag group (including ``(?:`` / ``(?=``
/// / ``(?P<`` forms, which parse_atom handles or rejects) is returned
/// unchanged.
std::optional<std::string>
strip_inline_flags(const std::string &p, bool &ignorecase, bool &dotall)
{
  ignorecase = false;
  dotall = false;
  if(p.size() < 3 || p[0] != '(' || p[1] != '?')
    return p;
  bool ic = false, da = false;
  std::size_t i = 2;
  for(; i < p.size() && p[i] != ')' && p[i] != ':'; ++i)
  {
    switch(p[i])
    {
    case 'i':
      ic = true;
      break;
    case 's':
      da = true;
      break;
    case 'a':
    case 'L':
    case 'm':
    case 'u':
    case 'x':
      return std::nullopt; // recognised but unmodelled flag → nondet
    default:
      return p; // not a flag group (e.g. (?P<name>, (?=, (?<=) → leave to parser
    }
  }
  if(i > 2 && i < p.size() && p[i] == ')')
  {
    ignorecase = ic;
    dotall = da;
    return p.substr(i + 1);
  }
  return p; // (?:...), (?...) without a closing flag list, etc.
}

/// Translate a pattern to the SMT regex for the whole subject string
/// under the given match semantics. Returns nullopt for unsupported
/// patterns (caller falls back to a sound nondet model).
std::optional<std::string> translate(
  const std::string &pattern,
  match_kind kind,
  regex_char_classest classes = python_regex_char_classes())
{
  bool ignorecase = false, dotall = false;
  auto core_in = strip_inline_flags(pattern, ignorecase, dotall);
  if(!core_in.has_value())
    return std::nullopt; // unsupported leading inline flag

  bool had_start, had_end;
  std::string core;
  strip_anchors(*core_in, had_start, had_end, core);
  translator t{core, ignorecase, dotall, std::move(classes)};
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

std::optional<std::string>
java_regex_to_smt_fullmatch(const std::string &pattern)
{
  // Java-dialect SEMANTICS (dot line terminators, \s) within the shared
  // parser; callers must also gate on regex_in_python_java_common_core so
  // only identically-PARSED syntax reaches this.
  return translate(pattern, match_kind::fullmatch, java_regex_char_classes());
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
  // nullopt), which is what a positional-context-free caller needs. A leading
  // inline-flag group (?i)/(?s) is honoured (and unmodelled flags bail).
  bool ignorecase = false, dotall = false;
  auto core = strip_inline_flags(pattern, ignorecase, dotall);
  if(!core.has_value())
    return std::nullopt;
  translator t{*core, ignorecase, dotall};
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

namespace
{
/// If ``frag`` is a pure literal run (only literal characters and escaped
/// metacharacters, no regex operators), return its decoded literal text;
/// otherwise ``std::nullopt``. ``\d`` / ``\w`` / ``\s`` etc. are NOT literals.
std::optional<std::string> regex_literal_text(const std::string &frag)
{
  static const std::string metas = "^$.|?*+()[]{}";
  std::string out;
  std::size_t i = 0;
  const std::size_t n = frag.size();
  while(i < n)
  {
    const char c = frag[i];
    if(c == '\\')
    {
      if(i + 1 >= n)
        return std::nullopt;
      const char e = frag[i + 1];
      // An escaped metacharacter denotes that literal character.
      if(metas.find(e) != std::string::npos || e == '\\' || e == '/')
      {
        out += e;
        i += 2;
        continue;
      }
      // \d \w \s \n ... are not plain literals -> not a pure-literal run.
      return std::nullopt;
    }
    if(metas.find(c) != std::string::npos)
      return std::nullopt;
    out += c;
    ++i;
  }
  return out;
}

/// Scan a ``[...]`` character class starting at ``frag[i]=='['``, returning the
/// index just past the closing ``]``, or ``std::nullopt`` if unterminated.
std::optional<std::size_t>
scan_char_class(const std::string &p, std::size_t i, std::size_t end)
{
  std::size_t j = i + 1;
  if(j < end && p[j] == '^')
    ++j;
  if(j < end && p[j] == ']') // a leading ']' is a literal class member
    ++j;
  while(j < end && p[j] != ']')
  {
    if(p[j] == '\\' && j + 1 < end)
      j += 2;
    else
      ++j;
  }
  if(j >= end)
    return std::nullopt;
  return j + 1; // past the ']'
}
} // namespace

std::optional<std::vector<python_regex_segmentt>>
python_regex_segment_groups(const std::string &pattern)
{
  const std::string &p = pattern;
  std::size_t start = 0;
  std::size_t end = p.size();

  // Strip a leading start-anchor and trailing end-anchor: the match wrappers
  // already account for them and the matched span decomposes identically. An
  // *unescaped* trailing '$' only.
  if(start < end && p[start] == '^')
    ++start;
  if(end > start && p[end - 1] == '$')
  {
    std::size_t bs = 0;
    std::size_t k = end - 1;
    while(k > start && p[k - 1] == '\\')
    {
      ++bs;
      --k;
    }
    if(bs % 2 == 0)
      --end;
  }

  std::vector<python_regex_segmentt> segs;
  std::string current; // accumulating non-group (literal/regex) run

  auto flush_current = [&]()
  {
    if(!current.empty())
    {
      python_regex_segmentt seg;
      seg.is_group = false;
      seg.sub_pattern = current;
      seg.literal = regex_literal_text(current);
      segs.push_back(seg);
      current.clear();
    }
  };

  std::size_t i = start;
  while(i < end)
  {
    const char c = p[i];
    if(c == '\\')
    {
      if(i + 1 >= end)
        return std::nullopt; // trailing backslash
      const char e = p[i + 1];
      if(std::isdigit(static_cast<unsigned char>(e)) || e == 'g')
        return std::nullopt; // back-reference
      if(e == 'b' || e == 'B' || e == 'A' || e == 'Z')
        return std::nullopt; // zero-width assertion breaks the linear model
      current += c;
      current += e;
      i += 2;
      continue;
    }
    if(c == '[')
    {
      auto j = scan_char_class(p, i, end);
      if(!j.has_value())
        return std::nullopt;
      current.append(p, i, *j - i);
      i = *j;
      continue;
    }
    if(c == '|')
      return std::nullopt; // top-level alternation
    if(c == '^' || c == '$')
      return std::nullopt; // mid-pattern anchor
    if(c == '(')
    {
      if(i + 1 < end && p[i + 1] == '?')
        return std::nullopt; // non-capturing / lookaround / named / flags
      flush_current();
      // Capture group: scan to the matching ')', bailing on any nested group.
      std::size_t j = i + 1;
      std::string inner;
      while(j < end && p[j] != ')')
      {
        const char d = p[j];
        if(d == '\\')
        {
          if(j + 1 >= end)
            return std::nullopt;
          inner += d;
          inner += p[j + 1];
          j += 2;
          continue;
        }
        if(d == '[')
        {
          auto k = scan_char_class(p, j, end);
          if(!k.has_value())
            return std::nullopt;
          inner.append(p, j, *k - j);
          j = *k;
          continue;
        }
        if(d == '(')
          return std::nullopt; // nested group
        // '|' inside the group is part of the group's own sub-pattern
        // (handled by the fragment translator), so it is fine here.
        inner += d;
        ++j;
      }
      if(j >= end)
        return std::nullopt; // unterminated group
      // A quantifier applied to the whole group would repeat it; the
      // single-occurrence concatenation model would then be unsound.
      if(j + 1 < end)
      {
        const char q = p[j + 1];
        if(q == '*' || q == '+' || q == '?' || q == '{')
          return std::nullopt;
      }
      python_regex_segmentt seg;
      seg.is_group = true;
      seg.sub_pattern = inner;
      seg.literal = std::nullopt;
      segs.push_back(seg);
      i = j + 1;
      continue;
    }
    current += c;
    ++i;
  }
  flush_current();

  // Nothing to extract unless there is at least one capture group.
  bool has_group = false;
  for(const auto &s : segs)
    if(s.is_group)
      has_group = true;
  if(!has_group)
    return std::nullopt;

  return segs;
}

// ---------------------------------------------------------------------------
// Conversion-time matcher (constant pattern + constant subject).
//
// A small recursive-descent parser builds an AST over the supported subset,
// and a continuation-passing backtracking matcher evaluates membership. The
// matcher decides only WHETHER a match exists (not which / how long), so
// greedy vs lazy quantifiers and capturing vs non-capturing groups are
// equivalent here. Any unsupported construct makes the parser fail, and the
// public entry then returns std::nullopt (the caller falls back to nondet --
// never a guessed boolean), keeping the fold sound.
// ---------------------------------------------------------------------------
namespace
{
struct mnode;
using mnodep = std::shared_ptr<mnode>;
struct mnode
{
  enum kindt
  {
    LIT,    ///< a single literal byte (`lit`)
    ANY,    ///< `.` (any byte except '\n')
    CLASS,  ///< `[...]` / `\d \w \s` (and negated)
    CONCAT, ///< sequence (`kids` in order)
    ALT,    ///< alternation (`kids`)
    REPEAT, ///< `child` repeated [`rmin`, `rmax`] (`rmax` < 0 = unbounded)
    BOL,    ///< `^` (zero-width: position 0)
    EOL,    ///< `$` (zero-width: end of subject)
  } kind;
  unsigned char lit = 0;
  std::vector<std::pair<unsigned char, unsigned char>> ranges; // CLASS
  bool negated = false;                                        // CLASS
  std::vector<mnodep> kids;                                    // CONCAT / ALT
  mnodep child;                                                // REPEAT
  int rmin = 0;
  int rmax = -1;
  bool ci = false;     ///< IGNORECASE for this LIT / CLASS
  bool dot_nl = false; ///< DOTALL: this ANY also matches '\n'
};

class re_match_parser
{
public:
  re_match_parser(
    const std::string &p,
    bool ic = false,
    bool da = false,
    regex_dialectt dl = regex_dialectt::python)
    : pat(p), ignorecase(ic), dotall(da), dialect(dl)
  {
  }

  // Parse the whole pattern; nullptr on any unsupported feature / syntax.
  mnodep parse()
  {
    mnodep n = parse_alt();
    if(failed || pos != pat.size())
      return nullptr;
    return n;
  }

private:
  const std::string &pat;
  std::size_t pos = 0;
  bool failed = false;
  const bool ignorecase = false;
  const bool dotall = false;
  const regex_dialectt dialect = regex_dialectt::python;

  bool eof() const
  {
    return pos >= pat.size();
  }
  char peek() const
  {
    return pat[pos];
  }
  void fail()
  {
    failed = true;
  }

  mnodep make(mnode::kindt k)
  {
    auto n = std::make_shared<mnode>();
    n->kind = k;
    // Carry the active flags onto the leaf nodes that honour them.
    if(k == mnode::LIT || k == mnode::CLASS)
      n->ci = ignorecase;
    else if(k == mnode::ANY)
      n->dot_nl = dotall;
    return n;
  }

  mnodep parse_alt()
  {
    std::vector<mnodep> alts;
    mnodep first = parse_concat();
    if(failed)
      return nullptr;
    alts.push_back(first);
    while(!eof() && peek() == '|')
    {
      ++pos;
      mnodep c = parse_concat();
      if(failed)
        return nullptr;
      alts.push_back(c);
    }
    if(alts.size() == 1)
      return alts[0];
    mnodep n = make(mnode::ALT);
    n->kids = std::move(alts);
    return n;
  }

  mnodep parse_concat()
  {
    mnodep n = make(mnode::CONCAT);
    while(!eof() && peek() != '|' && peek() != ')')
    {
      mnodep a = parse_quant();
      if(failed)
        return nullptr;
      if(a)
        n->kids.push_back(a);
    }
    return n;
  }

  mnodep parse_quant()
  {
    mnodep atom = parse_atom();
    if(failed || !atom)
      return atom;
    if(eof())
      return atom;
    int rmin, rmax;
    const char c = peek();
    if(c == '*')
    {
      ++pos;
      rmin = 0;
      rmax = -1;
    }
    else if(c == '+')
    {
      ++pos;
      rmin = 1;
      rmax = -1;
    }
    else if(c == '?')
    {
      ++pos;
      rmin = 0;
      rmax = 1;
    }
    else if(c == '{')
    {
      // {m} / {m,} / {m,n}; anything malformed -> bail (conservative).
      std::size_t save = pos;
      ++pos;
      std::string lo, hi;
      while(!eof() && std::isdigit((unsigned char)peek()))
        lo.push_back(pat[pos++]);
      bool has_comma = false;
      if(!eof() && peek() == ',')
      {
        has_comma = true;
        ++pos;
        while(!eof() && std::isdigit((unsigned char)peek()))
          hi.push_back(pat[pos++]);
      }
      if(eof() || peek() != '}' || lo.empty())
      {
        (void)save;
        fail();
        return nullptr;
      }
      ++pos;
      rmin = std::stoi(lo);
      rmax = has_comma ? (hi.empty() ? -1 : std::stoi(hi)) : rmin;
    }
    else
      return atom;
    // A trailing '?' (lazy) or '+' (possessive) changes which match is chosen,
    // not whether one exists -- accept lazy, bail on possessive.
    if(!eof() && peek() == '?')
      ++pos;
    else if(!eof() && peek() == '+')
    {
      fail();
      return nullptr;
    }
    mnodep n = make(mnode::REPEAT);
    n->child = atom;
    n->rmin = rmin;
    n->rmax = rmax;
    return n;
  }

  mnodep parse_atom()
  {
    if(eof())
      return nullptr;
    const char c = peek();
    if(c == '^')
    {
      ++pos;
      return make(mnode::BOL);
    }
    if(c == '$')
    {
      ++pos;
      return make(mnode::EOL);
    }
    if(c == '.')
    {
      ++pos;
      if(dialect == regex_dialectt::java && !dotall)
      {
        // Java '.' excludes \n \r (\u0085/\u2028/\u2029 are outside the
        // byte matcher's ASCII domain); Python's ANY excludes only \n.
        mnodep n = make(mnode::CLASS);
        n->negated = true;
        n->ranges.emplace_back('\n', '\n');
        n->ranges.emplace_back('\r', '\r');
        return n;
      }
      return make(mnode::ANY);
    }
    if(c == '(')
    {
      ++pos;
      if(!eof() && peek() == '?')
      {
        // Only the non-capturing group (?:...) is safe for a match decision;
        // lookaround / named / inline-flag groups are out of scope.
        ++pos;
        if(!eof() && peek() == ':')
          ++pos;
        else
        {
          fail();
          return nullptr;
        }
      }
      mnodep inner = parse_alt();
      if(failed)
        return nullptr;
      if(eof() || peek() != ')')
      {
        fail();
        return nullptr;
      }
      ++pos;
      // Capturing vs non-capturing is irrelevant to the match decision.
      return inner;
    }
    if(c == '[')
      return parse_class();
    if(c == '\\')
      return parse_escape();
    // A dangling metacharacter here is a malformed pattern for us.
    if(c == '*' || c == '+' || c == '?' || c == '{' || c == ')')
    {
      fail();
      return nullptr;
    }
    ++pos;
    mnodep n = make(mnode::LIT);
    n->lit = (unsigned char)c;
    return n;
  }

  // Append the ranges for a `\d \w \s` (or upper = negated complement base)
  // class to `out`. Returns false for an unknown class letter.
  static bool class_ranges_for(
    char letter,
    std::vector<std::pair<unsigned char, unsigned char>> &out,
    regex_dialectt dl = regex_dialectt::python)
  {
    switch(letter)
    {
    case 'd':
    case 'D':
      out.emplace_back('0', '9');
      return true;
    case 'w':
    case 'W':
      out.emplace_back('0', '9');
      out.emplace_back('A', 'Z');
      out.emplace_back('a', 'z');
      out.emplace_back('_', '_');
      return true;
    case 's':
    case 'S':
      // \t \n \v \f \r and space in both dialects...
      out.emplace_back(0x09, 0x0d);
      out.emplace_back(' ', ' ');
      // ...plus, in Python only, \x1c-\x1f (FS GS RS US: CPython's Unicode
      // whitespace within ASCII; Java's Pattern \s excludes them).
      if(dl == regex_dialectt::python)
        out.emplace_back(0x1c, 0x1f);
      return true;
    default:
      return false;
    }
  }

  // Decode a backslash escape to a literal byte, or return false (unsupported).
  static bool escape_literal(char e, unsigned char &out)
  {
    switch(e)
    {
    case 'n':
      out = '\n';
      return true;
    case 't':
      out = '\t';
      return true;
    case 'r':
      out = '\r';
      return true;
    case 'f':
      out = '\f';
      return true;
    case 'v':
      out = '\v';
      return true;
    // Escaped metacharacters / punctuation are literals.
    case '.':
    case '\\':
    case '(':
    case ')':
    case '[':
    case ']':
    case '{':
    case '}':
    case '*':
    case '+':
    case '?':
    case '|':
    case '^':
    case '$':
    case '-':
    case '/':
      out = (unsigned char)e;
      return true;
    default:
      return false;
    }
  }

  mnodep parse_escape()
  {
    ++pos; // consume '\\'
    if(eof())
    {
      fail();
      return nullptr;
    }
    const char e = pat[pos++];
    std::vector<std::pair<unsigned char, unsigned char>> r;
    if(class_ranges_for(e, r, dialect))
    {
      mnodep n = make(mnode::CLASS);
      n->ranges = std::move(r);
      n->negated = std::isupper((unsigned char)e); // \D \W \S
      return n;
    }
    unsigned char lit;
    if(escape_literal(e, lit))
    {
      mnodep n = make(mnode::LIT);
      n->lit = lit;
      return n;
    }
    // Back-references (\1..\9), \b \B \A \Z \G, \x.. etc.: out of scope.
    fail();
    return nullptr;
  }

  mnodep parse_class()
  {
    ++pos; // consume '['
    mnodep n = make(mnode::CLASS);
    if(!eof() && peek() == '^')
    {
      n->negated = true;
      ++pos;
    }
    bool first = true;
    while(!eof() && (peek() != ']' || first))
    {
      first = false;
      unsigned char lo;
      if(peek() == '\\')
      {
        ++pos;
        if(eof())
        {
          fail();
          return nullptr;
        }
        const char e = pat[pos++];
        std::vector<std::pair<unsigned char, unsigned char>> sub;
        if(class_ranges_for(e, sub, dialect))
        {
          // A class shorthand inside [...] (e.g. [\d.]). Negated shorthands
          // inside a class are not handled precisely -> bail.
          if(std::isupper((unsigned char)e))
          {
            fail();
            return nullptr;
          }
          for(const auto &s : sub)
            n->ranges.push_back(s);
          continue;
        }
        if(!escape_literal(e, lo))
        {
          fail();
          return nullptr;
        }
      }
      else
        lo = (unsigned char)pat[pos++];
      // Range lo-hi?
      if(!eof() && peek() == '-' && pos + 1 < pat.size() && pat[pos + 1] != ']')
      {
        ++pos; // consume '-'
        unsigned char hi;
        if(peek() == '\\')
        {
          ++pos;
          if(eof() || !escape_literal(pat[pos], hi))
          {
            fail();
            return nullptr;
          }
          ++pos;
        }
        else
          hi = (unsigned char)pat[pos++];
        if(hi < lo)
        {
          fail();
          return nullptr;
        }
        n->ranges.emplace_back(lo, hi);
      }
      else
        n->ranges.emplace_back(lo, lo);
    }
    if(eof() || peek() != ']')
    {
      fail();
      return nullptr;
    }
    ++pos;
    return n;
  }
};

// Continuation-passing backtracking matcher. `k(end)` is the rest of the
// match; returns true if some match of `n` from `at` lets `k` succeed.
static bool re_seq(
  const std::vector<mnodep> &seq,
  std::size_t i,
  const std::string &s,
  std::size_t at,
  const std::function<bool(std::size_t)> &k);

static bool re_do(
  const mnodep &n,
  const std::string &s,
  std::size_t at,
  const std::function<bool(std::size_t)> &k);

static bool re_rep(
  const mnodep &n,
  int cnt,
  const std::string &s,
  std::size_t at,
  const std::function<bool(std::size_t)> &k)
{
  const bool can_more = (n->rmax < 0 || cnt < n->rmax);
  if(
    can_more &&
    re_do(
      n->child,
      s,
      at,
      [&, cnt, at](std::size_t np)
      {
        if(np == at)
          return false; // empty match: stop expanding (would not terminate)
        return re_rep(n, cnt + 1, s, np, k);
      }))
    return true;
  if(cnt >= n->rmin)
    return k(at);
  return false;
}

static bool re_do(
  const mnodep &n,
  const std::string &s,
  std::size_t at,
  const std::function<bool(std::size_t)> &k)
{
  switch(n->kind)
  {
  case mnode::BOL:
    return at == 0 && k(at);
  case mnode::EOL:
    return at == s.size() && k(at);
  case mnode::LIT:
  {
    if(at >= s.size())
      return false;
    const unsigned char c = (unsigned char)s[at];
    const bool eq =
      c == n->lit || (n->ci && std::tolower(c) == std::tolower(n->lit));
    return eq && k(at + 1);
  }
  case mnode::ANY:
    return at < s.size() && (n->dot_nl || s[at] != '\n') && k(at + 1);
  case mnode::CLASS:
  {
    if(at >= s.size())
      return false;
    const unsigned char ch = (unsigned char)s[at];
    auto in_ranges = [&](unsigned char x)
    {
      for(const auto &r : n->ranges)
        if(x >= r.first && x <= r.second)
          return true;
      return false;
    };
    bool in = in_ranges(ch);
    // IGNORECASE: an alphabetic char also matches via its other case.
    if(!in && n->ci && std::isalpha(ch))
      in = in_ranges((
        unsigned char)(std::isupper(ch) ? std::tolower(ch) : std::toupper(ch)));
    if(n->negated)
      in = !in;
    return in && k(at + 1);
  }
  case mnode::CONCAT:
    return re_seq(n->kids, 0, s, at, k);
  case mnode::ALT:
    for(const auto &c : n->kids)
      if(re_do(c, s, at, k))
        return true;
    return false;
  case mnode::REPEAT:
    return re_rep(n, 0, s, at, k);
  }
  return false;
}

static bool re_seq(
  const std::vector<mnodep> &seq,
  std::size_t i,
  const std::string &s,
  std::size_t at,
  const std::function<bool(std::size_t)> &k)
{
  if(i == seq.size())
    return k(at);
  return re_do(
    seq[i],
    s,
    at,
    [&, i](std::size_t np) { return re_seq(seq, i + 1, s, np, k); });
}
} // namespace

std::optional<bool> python_regex_match(
  const std::string &pattern,
  const std::string &subject,
  python_regex_match_kindt kind,
  regex_dialectt dialect)
{
  // Multiline `^`/`$`/`.` edge cases are not modelled: bail on a newline in
  // the subject so the matcher's single-line anchoring stays exact.
  if(subject.find('\n') != std::string::npos)
    return std::nullopt;

  // Honour a leading inline-flag group `(?i)` / `(?s)` / `(?is)` (the re stub
  // maps the `flags=` argument to this prefix). An unmodelled flag letter
  // (a/L/u/x) makes strip_inline_flags return nullopt -> sound nondet. (`m`
  // / MULTILINE is a no-op here since we already bail on newline subjects.)
  bool ignorecase = false, dotall = false;
  const std::optional<std::string> core =
    strip_inline_flags(pattern, ignorecase, dotall);
  if(!core.has_value())
    return std::nullopt;

  re_match_parser parser{*core, ignorecase, dotall, dialect};
  const mnodep ast = parser.parse();
  if(!ast)
    return std::nullopt;

  if(kind == python_regex_match_kindt::FULLMATCH)
    return re_do(
      ast, subject, 0, [&](std::size_t e) { return e == subject.size(); });
  if(kind == python_regex_match_kindt::MATCH)
    return re_do(ast, subject, 0, [](std::size_t) { return true; });
  // SEARCH: unanchored -- try every start offset.
  for(std::size_t i = 0; i <= subject.size(); ++i)
    if(re_do(ast, subject, i, [](std::size_t) { return true; }))
      return true;
  return false;
}

std::optional<std::pair<int, int>> python_regex_search_pos(
  const std::string &pattern,
  const std::string &subject,
  int from)
{
  if(subject.find('\n') != std::string::npos)
    return std::nullopt;
  bool ic = false, da = false;
  const std::optional<std::string> core = strip_inline_flags(pattern, ic, da);
  if(!core.has_value())
    return std::nullopt;
  re_match_parser parser{*core, ic, da};
  const mnodep ast = parser.parse();
  if(!ast)
    return std::nullopt;

  const int n = static_cast<int>(subject.size());
  for(int i = (from < 0 ? 0 : from); i <= n; ++i)
  {
    // The continuation records the FIRST reached end. Because the matcher
    // explores greedy- (more repetitions) and alternation- (first branch)
    // first, that first end is CPython's leftmost/greedy match end.
    std::optional<std::size_t> endp;
    re_do(
      ast,
      subject,
      (std::size_t)i,
      [&](std::size_t e)
      {
        if(!endp.has_value())
          endp = e;
        return true;
      });
    if(endp.has_value())
      return std::make_pair(i, (int)*endp);
  }
  return std::make_pair(-1, -1);
}

std::optional<std::string> python_regex_sub(
  const std::string &pattern,
  const std::string &repl,
  const std::string &subject,
  int count)
{
  // A backslash in `repl` is a group/escape reference (\1, \g<..>, \\) -- not
  // modelled here; bail to a sound nondet.
  if(repl.find('\\') != std::string::npos)
    return std::nullopt;
  if(subject.find('\n') != std::string::npos)
    return std::nullopt;

  std::string out;
  int pos = 0;
  int done = 0;
  const int n = static_cast<int>(subject.size());
  while(pos <= n)
  {
    if(count > 0 && done >= count)
      break;
    const std::optional<std::pair<int, int>> m =
      python_regex_search_pos(pattern, subject, pos);
    if(!m.has_value())
      return std::nullopt; // unsupported pattern -> sound nondet
    const int st = m->first;
    const int en = m->second;
    if(st < 0)
      break; // no more matches
    out.append(subject, (std::size_t)pos, (std::size_t)(st - pos));
    out.append(repl);
    done += 1;
    if(en > pos)
      pos = en;
    else
    {
      // Empty match: emit the current char and advance by one (CPython).
      if(st < n)
        out.push_back(subject[(std::size_t)st]);
      pos = st + 1;
    }
  }
  if(pos < n)
    out.append(subject, (std::size_t)pos, std::string::npos);
  return out;
}

bool regex_in_python_java_common_core(const std::string &pattern)
{
  // Conservative lexical scan: accept only constructs both dialects parse
  // identically; reject (return false) anything Java-divergent or unclear.
  // The translator itself then decides translatability within the core.
  for(std::size_t i = 0; i < pattern.size(); ++i)
  {
    const char c = pattern[i];
    if(c == '\\')
    {
      if(i + 1 >= pattern.size())
        return false;
      const char e = pattern[++i];
      // Shared escapes: char classes, the common control chars, and
      // punctuation literals. NOTE deliberate exclusions -- \v (single char
      // 0x0b in Python, vertical-whitespace CLASS in Java), \h/\H, \R, \z,
      // \Q/\E, \p/\P, \b/\B/\A/\Z (assertions; translator rejects anyway),
      // \0..\9 (backreferences / octal).
      static const std::string shared_escapes = "dDsSwWtnrf";
      if(shared_escapes.find(e) != std::string::npos)
        continue;
      if(std::isalnum(static_cast<unsigned char>(e)))
        return false;
      continue; // escaped punctuation: literal in both dialects
    }
    if(c == '[')
    {
      // Scan the class body: reject Java's && intersection and any nested
      // class opener; allow shared escapes and ranges.
      ++i;
      if(i < pattern.size() && pattern[i] == '^')
        ++i;
      if(i < pattern.size() && pattern[i] == ']')
        ++i; // leading ] is a literal in both dialects
      bool closed = false;
      for(; i < pattern.size(); ++i)
      {
        if(pattern[i] == '\\' && i + 1 < pattern.size())
        {
          ++i;
          continue;
        }
        if(pattern[i] == ']')
        {
          closed = true;
          break;
        }
        if(pattern[i] == '&' && i + 1 < pattern.size() && pattern[i + 1] == '&')
          return false; // Java class intersection; literal &s in Python
        if(pattern[i] == '[')
          return false; // Java nested class ([a[b]]); literal [ in Python
      }
      if(!closed)
        return false;
      continue;
    }
    if(c == '*' || c == '+' || c == '?')
    {
      // Possessive quantifiers (*+, ++, ?+, {m,n}+) are Java-only and change
      // the matched LANGUAGE; a following '+' must be rejected. (A following
      // '?' -- lazy -- is shared and language-preserving.)
      if(i + 1 < pattern.size() && pattern[i + 1] == '+')
        return false;
      continue;
    }
    if(c == '}')
    {
      if(i + 1 < pattern.size() && pattern[i + 1] == '+')
        return false; // {m,n}+ possessive
      continue;
    }
    if(c == '(')
    {
      // Groups: plain and (?: (?= (?! etc. -- the translator handles or
      // rejects those uniformly; but Java's (?< named groups vs Python's
      // (?P< differ in SYNTAX (both would mis-parse the other's), so reject
      // any (?P or (?< here.
      if(i + 2 < pattern.size() && pattern[i + 1] == '?')
      {
        const char g = pattern[i + 2];
        if(g == 'P' || g == '<')
          return false;
      }
      continue;
    }
  }
  return true;
}
