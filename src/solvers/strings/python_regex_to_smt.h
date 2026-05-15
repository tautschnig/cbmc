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

#endif // CPROVER_SOLVERS_STRINGS_PYTHON_REGEX_TO_SMT_H
