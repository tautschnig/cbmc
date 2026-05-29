/// \file
/// TypeScript RegExp Phase 2: NFA construction and simulation for
/// metacharacter-supported regex matching against constant strings.
///
/// Supported metacharacters:
///   .            any single character (except newline)
///   *  +  ?      Kleene star, plus, optional (greedy)
///   [abc]        character class (positive)
///   [^abc]       character class (negated)
///   [a-z]        ranges within character classes
///   \d \w \s     shorthand classes (digit, word, whitespace)
///   \D \W \S     negated shorthand classes
///   ^  $         anchors (begin/end of string; multiline not supported)
///   \\           literal escape for metacharacters (\\., \\*, \\\\, etc.)
///
/// Deferred to Phase 2b:
///   |            alternation
///   ()           grouping (non-capturing)
///   {n,m}        bounded quantifiers
///
/// Deferred to Phase 3 (SMT-string):
///   symbolic input matching
///
/// The engine is constant-only: the compiled NFA simulates over a
/// constant std::string and returns true/false. Symbolic input
/// continues to fall through to the nondet path (Phase 1 behaviour).

#ifndef CPROVER_TYPESCRIPT_TYPESCRIPT_REGEX_H
#define CPROVER_TYPESCRIPT_TYPESCRIPT_REGEX_H

#include <optional>
#include <string>

namespace typescript_regex
{
/// Try to match `pattern` (regex source, without surrounding `/`)
/// against `input` (constant string).
///
/// \return std::nullopt if `pattern` uses an unsupported feature
/// (the caller should fall back to a nondet result), `true` if the
/// regex matches anywhere in `input` (RegExp.prototype.test
/// semantics — partial match unless anchored), `false` otherwise.
std::optional<bool> match(const std::string &pattern, const std::string &input);

/// Returns true iff `pattern` is parseable by this engine. Used by
/// the dispatch site to decide between Phase 1 (literal) and Phase
/// 2 (NFA) without doing the full match.
bool is_supported(const std::string &pattern);

} // namespace typescript_regex

#endif // CPROVER_TYPESCRIPT_TYPESCRIPT_REGEX_H
