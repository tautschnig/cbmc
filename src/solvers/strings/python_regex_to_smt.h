/*******************************************************************\

Module: Translate a Python regular-expression pattern to an SMT-LIB
        2.6 regex term.

Author: Wave 2 of Python re support.

\*******************************************************************/

/// \file
/// Translate a Python regular-expression pattern string to an
/// SMT-LIB 2.6 regex term. The output is intended for use with the
/// SMT-LIB theory of strings' ``str.in_re`` operator.
///
/// Scope: character classes, quantifiers, alternation, anchors,
/// basic escapes. Back-references, lookahead/lookbehind, named
/// captures and flags are out of scope — callers of the translator
/// should detect these unsupported features ahead of the call (by
/// returning ``std::nullopt``) and fall back to a nondet model.

#ifndef CPROVER_SOLVERS_STRINGS_PYTHON_REGEX_TO_SMT_H
#define CPROVER_SOLVERS_STRINGS_PYTHON_REGEX_TO_SMT_H

#include <optional>
#include <string>
#include <vector>

/// One top-level fragment of a pattern, as produced by
/// \ref python_regex_segment_groups. The fragments concatenate (in order) to
/// the whole match, so the matched text decomposes as
/// ``text == frag[0] ++ frag[1] ++ ...``.
struct python_regex_segmentt
{
  /// ``true`` iff this fragment is a numbered capture group ``(...)`` (the
  /// thing ``Match.group(n)`` returns); ``false`` for the literal/regex runs
  /// between/around groups.
  bool is_group;
  /// The fragment's sub-pattern text (the group's inner pattern, or the
  /// inter-group run), suitable for an ``re.fullmatch`` constraint on the
  /// fragment.
  std::string sub_pattern;
  /// When the fragment is a pure literal run (no regex metacharacters), its
  /// decoded literal text; ``std::nullopt`` otherwise. A caller can pin a
  /// pure-literal fragment to this constant instead of an ``in_re`` constraint.
  std::optional<std::string> literal;
};

/// Segment ``pattern`` into the top-level sequence of capture groups and the
/// literal/regex runs around them, for ``Match.group(n)`` extraction by
/// ``str.++`` decomposition (see doc/python-frontend-regex-position-plan.md,
/// Phase 3). The fragments concatenate to the whole match.
///
/// Returns ``std::nullopt`` when ``pattern`` is outside the safely-
/// decomposable subset, so the caller keeps a sound nondet result:
///   - top-level alternation (``a|b``),
///   - a quantifier applied to a group (``(...)*`` / ``(...)+`` / ``(...)?`` /
///     ``(...){m,n}``) — the linear one-occurrence model would be wrong,
///   - nested groups (``((...))``),
///   - non-capturing / lookaround / named groups (``(?:...)``, ``(?=...)``,
///     ``(?P<n>...)``) — conservatively rejected,
///   - anchors (``^`` / ``$``) and word boundaries,
///   - back-references (``\1`` .. ``\9``, ``\g<...>``).
/// The decomposition itself stays sound for any fragment whose sub-pattern the
/// SMT translator cannot handle (the fragment is left unconstrained, i.e. an
/// over-approximation); the bail conditions above are only those that would
/// make the linear concatenation model itself unsound.
/// Character-class definitions (as SMT-LIB RegLan terms) for a regex dialect.
/// The recursive-descent translator is dialect-agnostic apart from these
/// escape-class expansions; parameterising them lets the same translator serve
/// ASCII regex today and (e.g.) a Unicode dialect later. `\\D`/`\\S`/`\\W`
/// are derived as `(re.diff re.allchar <class>)`.
struct regex_char_classest
{
  std::string digit;      ///< \\d
  std::string whitespace; ///< \\s
  std::string word;       ///< \\w
  /// The characters `.` does NOT match (without DOTALL), as a RegLan term.
  std::string dot_excluded;
};

/// Python `re` character classes, restricted to the code points below 0x80
/// that the byte-oriented string model represents exactly. Per the Python
/// Language Reference, \\s is the Unicode whitespace set -- within ASCII that
/// is [\\t\\n\\v\\f\\r\\x1c-\\x1f ] (NOTE: includes the separators
/// \\x1c-\\x1f, unlike Java's \\s); \\d=[0-9], \\w=[A-Za-z0-9_] on ASCII.
/// Code points >= 0x80 (U+0085, U+00A0, Arabic-Indic digits, ...) are
/// multi-byte in the UTF-8 byte model and remain out of scope (documented
/// model boundary). `.` excludes only \\n.
regex_char_classest python_regex_char_classes();

/// Java `java.util.regex.Pattern` character classes (no UNICODE_CHARACTER_
/// CLASS flag): per the Pattern javadoc \\d=[0-9], \\s=[ \\t\\n\\x0B\\f\\r]
/// (NO \\x1c-\\x1f -- differs from BOTH Python's \\s and Java's own
/// Character.isWhitespace), \\w=[a-zA-Z_0-9]. `.` excludes the Java line
/// terminators \\n \\r \\u0085 \\u2028 \\u2029 (exact here: JBMC chars are
/// UTF-16 code units, 1:1 for the BMP).
regex_char_classest java_regex_char_classes();

std::optional<std::vector<python_regex_segmentt>>
python_regex_segment_groups(const std::string &pattern);

/// Translate a Python regex ``pattern`` to an SMT-LIB 2.6 regex
/// term. Returns ``std::nullopt`` when the pattern contains a
/// feature the translator doesn't support (back-references,
/// lookaround, named captures, non-ASCII escapes, etc.).
///
/// The returned term, when used with ``(str.in_re s <term>)``, has
/// the same semantics as Python's ``re.fullmatch(pattern, s)``
/// (i.e. the whole string must match). Callers wanting
/// ``re.match`` (anchored at the start) should wrap with
/// ``(re.++ <term> (re.* re.allchar))``; callers wanting
/// ``re.search`` (unanchored) should wrap with
/// ``(re.++ (re.* re.allchar) <term> (re.* re.allchar))``.
std::optional<std::string>
python_regex_to_smt_fullmatch(const std::string &pattern);

/// Java-dialect translation of \p pattern (java.util.regex semantics) to an
/// SMT-LIB regex term with String.matches / Matcher.matches (full-match)
/// anchoring. Callers must ALSO gate on
/// regex_in_python_java_common_core(pattern): the shared parser only accepts
/// syntax both dialects parse identically, and this entry supplies the
/// Java-dialect SEMANTICS for the constructs whose meaning diverges within
/// that core (`.` line terminators, \\s).
std::optional<std::string>
java_regex_to_smt_fullmatch(const std::string &pattern);

/// True when \p pattern uses only the Perl-derived COMMON CORE on which the
/// Python and Java regex dialects agree (literals, ., [...] classes without
/// Java's && intersection, groups, alternation, greedy/lazy quantifiers,
/// {m,n}, ^/$, and the shared escapes \d\D\s\S\w\W \t\n\r\f and punctuation
/// literals). False for any construct whose SEMANTICS or PARSE differs
/// between the dialects (Java: [a&&b] intersection, \p{...}/\P{...},
/// \Q...\E quoting, \v/\V vertical-whitespace classes, \h/\H, \R, \z, \G,
/// possessive quantifiers) -- a caller translating a JAVA pattern with the
/// (Python-dialect) translator must reject those rather than silently
/// mis-model them.
bool regex_in_python_java_common_core(const std::string &pattern);

/// Convenience: return the SMT-LIB regex term equivalent to
/// Python's ``re.match(pattern, s)`` semantics (start-anchored,
/// end-unanchored).
std::optional<std::string>
python_regex_to_smt_match(const std::string &pattern);

/// Convenience: return the SMT-LIB regex term equivalent to
/// Python's ``re.search(pattern, s)`` semantics (unanchored on
/// both ends).
std::optional<std::string>
python_regex_to_smt_search(const std::string &pattern);

/// Return ``true`` iff ``pattern`` can match the empty string,
/// ``false`` if it definitely cannot, ``std::nullopt`` for
/// unsupported patterns (back-references, lookaround, etc.) where
/// the answer can't be determined statically.
///
/// This is the ε-acceptance test for the regex's underlying
/// language. For anchored / unanchored variants it doesn't
/// matter: ``re.search(r, "")`` succeeds iff the pattern's
/// language contains the empty string, and so does
/// ``re.match`` / ``re.fullmatch``.
///
/// Examples:
///   "^foo$"           → false
///   "^a*$"            → true
///   "^(a|b*)$"        → true   (b* accepts empty)
///   "^a+(b)?$"        → false  (a+ requires at least one a)
///   "^"               → true
///   "^arn:.*"         → false  (literal arn: prefix is required)
std::optional<bool> python_regex_can_match_empty(const std::string &pattern);

/// Translate ``pattern`` to a bare SMT-LIB regex term for its language,
/// with no anchoring or fullmatch/match/search wrapping. Returns
/// ``std::nullopt`` for unsupported patterns, and also for any pattern
/// containing a ``^`` / ``$`` anchor (a bare regex term cannot express a
/// positional assertion). Intended for ``str.in_re`` / ``str.replace_re_all``
/// callers that supply their own context.
std::optional<std::string> python_regex_to_smt_body(const std::string &pattern);

/// If every string in ``pattern``'s language has the same length ``L``,
/// return ``L``; return ``std::nullopt`` for variable-length patterns
/// (unbounded or optional quantifiers, unequal-length alternation, ``{m,n}``
/// with ``m != n``) and for unsupported patterns. A fixed length ``>= 1`` is
/// the condition under which ``str.replace_re_all`` (which selects the
/// leftmost-shortest match) coincides with CPython ``re.sub`` (leftmost-
/// longest / greedy): when the match length is forced the two agree and there
/// are no empty matches.
std::optional<int> python_regex_fixed_length(const std::string &pattern);

/// Which anchoring semantics to evaluate, mirroring the `re` functions.
enum class python_regex_match_kindt
{
  FULLMATCH, ///< `re.fullmatch`: the whole subject must match.
  MATCH,     ///< `re.match`: anchored at the start, end unanchored.
  SEARCH,    ///< `re.search`: unanchored on both ends.
};

/// Decide, at conversion time, whether `subject` matches `pattern` under the
/// given anchoring -- i.e. evaluate `re.{fullmatch,match,search}(pattern,
/// subject)` for CONSTANT pattern and subject. Returns the precise boolean
/// result, or `std::nullopt` when the pattern uses a feature this matcher does
/// not support (back-references, lookaround, named/inline-flag groups, word
/// boundaries, unknown escapes) or the subject contains a newline (the matcher
/// does not model multiline `^`/`$`/`.` edge cases). A `std::nullopt` result
/// means "undecided" -- the caller must fall back to a sound nondet, never to a
/// guessed boolean. Greedy/lazy quantifiers collapse here (they change WHICH
/// match is chosen, not WHETHER one exists). Used to make the default backend
/// return `Match()`/`None` precisely for literal/known-structure regexes
/// without an SMT String solver.
/// Regex dialect for the concrete byte matcher: affects `.`'s excluded line
/// terminators and the \\s class (within ASCII, the matcher's domain).
enum class regex_dialectt
{
  python,
  java
};

std::optional<bool> python_regex_match(
  const std::string &pattern,
  const std::string &subject,
  python_regex_match_kindt kind,
  regex_dialectt dialect = regex_dialectt::python);

/// Leftmost match of `pattern` in `subject` at or after offset `from`, for a
/// CONSTANT pattern and subject. Returns the `{start, end}` byte offsets
/// (Python's leftmost / greedy match -- the matcher explores greedy- and
/// alternation-first, so the first complete match equals CPython's), or
/// `{-1, -1}` when there is no match at/after `from`, or `std::nullopt` when
/// the pattern is unsupported / the subject has a newline (caller falls back
/// to a sound nondet). Used to fold `re.findall` / `re.split`'s position
/// scan on the default backend.
std::optional<std::pair<int, int>> python_regex_search_pos(
  const std::string &pattern,
  const std::string &subject,
  int from);

/// `re.sub(pattern, repl, subject, count)` for CONSTANT arguments: replace the
/// leftmost non-overlapping matches of `pattern` (greedy) with the literal
/// `repl` (no group references), up to `count` (0 = all). Returns the result
/// string, or `std::nullopt` when the pattern is unsupported, `repl` contains a
/// backslash group/escape reference, or the subject has a newline (sound
/// nondet fallback). Empty matches advance by one position, as in CPython.
std::optional<std::string> python_regex_sub(
  const std::string &pattern,
  const std::string &repl,
  const std::string &subject,
  int count);

#endif // CPROVER_SOLVERS_STRINGS_PYTHON_REGEX_TO_SMT_H
