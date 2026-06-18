/*******************************************************************\

Module: Unit tests for python_regex_match in
        solvers/strings/python_regex_to_smt.h

Author: Python re support — default-backend constant-fold.

\*******************************************************************/

#include <solvers/strings/python_regex_to_smt.h>
#include <testing-utils/use_catch.h>

using k = python_regex_match_kindt;

// Expect a precise decision equal to `expected`.
static void req(
  const std::string &pat,
  const std::string &subj,
  python_regex_match_kindt kind,
  bool expected)
{
  INFO("pattern=" << pat << " subject=" << subj);
  auto r = python_regex_match(pat, subj, kind);
  REQUIRE(r.has_value());
  REQUIRE(*r == expected);
}

// Expect "undecided" (caller falls back to a sound nondet).
static void req_bail(const std::string &pat, const std::string &subj)
{
  INFO("pattern=" << pat << " subject=" << subj);
  REQUIRE_FALSE(python_regex_match(pat, subj, k::MATCH).has_value());
}

TEST_CASE(
  "python_regex_match literals",
  "[core][solvers][strings][python_regex]")
{
  req("abc", "abc", k::FULLMATCH, true);
  req("abc", "abcd", k::FULLMATCH, false);
  req("abc", "ab", k::FULLMATCH, false);
  req("abc", "abcdef", k::MATCH, true); // start-anchored prefix
  req("xyz", "abcdef", k::MATCH, false);
  req("bc", "abcdef", k::SEARCH, true);
  req("zz", "abc", k::SEARCH, false);
  // Empty pattern.
  req("", "", k::FULLMATCH, true);
  req("", "abc", k::FULLMATCH, false);
  req("", "abc", k::MATCH, true); // empty matches at start
  req("", "abc", k::SEARCH, true);
}

TEST_CASE(
  "python_regex_match classes + quantifiers",
  "[core][solvers][strings][python_regex]")
{
  req("[a-z]+", "abc", k::MATCH, true);
  req("[a-z]+", "123", k::MATCH, false);
  req("[A-Z]+", "ABC", k::FULLMATCH, true);
  req("[0-9]*", "12345", k::FULLMATCH, true);
  req("[0-9]*", "", k::FULLMATCH, true); // * allows empty
  req("\\d+", "123", k::FULLMATCH, true);
  req("\\d+", "123abc", k::FULLMATCH, false);
  req("\\d+", "123abc", k::MATCH, true); // prefix match
  req("\\d+", "abc", k::MATCH, false);
  req("[^0-9]+", "abc", k::FULLMATCH, true);
  req("[^0-9]+", "12", k::MATCH, false);
  // {m,n}
  req("a{2,3}", "a", k::FULLMATCH, false);
  req("a{2,3}", "aa", k::FULLMATCH, true);
  req("a{2,3}", "aaa", k::FULLMATCH, true);
  req("a{2,3}", "aaaa", k::FULLMATCH, false);
  req("a{2,3}", "aaaa", k::MATCH, true); // matches prefix "aaa"
  req("a{2}", "aa", k::FULLMATCH, true);
  req("a{2}", "aaa", k::FULLMATCH, false);
}

TEST_CASE(
  "python_regex_match dot/alternation/anchors",
  "[core][solvers][strings][python_regex]")
{
  req(".*", "", k::MATCH, true);
  req(".*", "whatever", k::FULLMATCH, true);
  req(".*", "xyz", k::SEARCH, true);
  req("a.*", "abc", k::MATCH, true);
  req("b.*", "abc", k::MATCH, false);
  req(".", "", k::MATCH, false); // . needs one char
  req("(a|b)", "a", k::FULLMATCH, true);
  req("(a|b)", "b", k::FULLMATCH, true);
  req("(a|b)", "c", k::FULLMATCH, false);
  req("(ab|cd)", "cd", k::FULLMATCH, true);
  req("(?:ab)+", "abab", k::FULLMATCH, true);
  req("(?:ab)+", "aba", k::FULLMATCH, false);
  // Anchors.
  req("abc$", "abc", k::MATCH, true);
  req("abc$", "abcd", k::MATCH, false); // $ forces end
  req("abc$", "xabc", k::SEARCH, true);
  req("abc$", "abcx", k::SEARCH, false);
  req("^abc", "abc", k::SEARCH, true);
  req("^abc", "xabc", k::SEARCH, false); // ^ forces start
  req("^[0-9]+$", "12345", k::FULLMATCH, true);
  req("^[0-9]+$", "12a45", k::FULLMATCH, false);
}

TEST_CASE(
  "python_regex_match inline flags",
  "[core][solvers][strings][python_regex]")
{
  // IGNORECASE via (?i).
  req("(?i)abc", "ABC", k::FULLMATCH, true);
  req("(?i)abc", "AbC", k::FULLMATCH, true);
  req("(?i)abc", "abc", k::FULLMATCH, true);
  req("(?i)abc", "abd", k::FULLMATCH, false);
  req("(?i)[a-z]+", "HELLO", k::FULLMATCH, true);
  req("(?i)[a-z]+", "Hello", k::FULLMATCH, true);
  req("abc", "ABC", k::FULLMATCH, false); // no flag -> case-sensitive
  // DOTALL via (?s) parses and is honoured (no-op for `.` on newline-free).
  req("(?s)a.c", "axc", k::FULLMATCH, true);
  // (?is) combined.
  req("(?is)a.C", "AXc", k::FULLMATCH, true);
  // Unmodelled flag letter -> bail (sound nondet).
  req_bail("(?x)abc", "abc");
  req_bail("(?a)\\d", "1");
}

TEST_CASE(
  "python_regex_match unsupported patterns bail",
  "[core][solvers][strings][python_regex]")
{
  req_bail("(a)\\1", "aa");       // back-reference
  req_bail("a(?=b)", "ab");       // lookahead
  req_bail("a(?!b)", "ac");       // negative lookahead
  req_bail("(?P<x>a)", "a");      // named group
  req_bail("\\bword\\b", "word"); // word boundary
  req_bail("a*+", "aaa");         // possessive
  // Newline in subject is out of scope (multiline anchoring).
  REQUIRE_FALSE(python_regex_match("a", "a\nb", k::SEARCH).has_value());
}
