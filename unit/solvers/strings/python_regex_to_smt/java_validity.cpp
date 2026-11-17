/*******************************************************************\

Module: Unit tests for the Java regex translation of
        solvers/strings/python_regex_to_smt.h

Author: Kiro

\*******************************************************************/

/// \file
/// The patterns that java.util.regex.Pattern rejects (measured on Java 8 and
/// 25) are not translated: the models let a regex that the translation does
/// not support throw a PatternSyntaxException, so a translated pattern must
/// be valid.

#include <solvers/strings/python_regex_to_smt.h>
#include <testing-utils/use_catch.h>

TEST_CASE(
  "Java patterns that Pattern rejects are not translated",
  "[core][solvers][strings][python_regex_to_smt]")
{
  for(const char *pattern : {
        "|+?",    "a{",     "(ab",      "ab)",      "[a-",  "*a",    "a**",
        "\\",     "a{2,1}", "[z-a]",    "+",        "?",    "a|*",   "(*)",
        "(?",     "(?x",    "[]",       "[",        "a{1",  "a{,2}", "\\k",
        "(?<n>a", "a+++",   "[a-\\d]",  "\\c",      "\\x",  "\\xZZ", "\\u12",
        "\\0",    "\\p",    "\\p{Foo}", "(?<1a>x)", "(|*)", "[\\]",
      })
  {
    INFO("pattern=" << pattern);
    CHECK_FALSE(java_regex_to_smt_fullmatch(pattern).has_value());
    CHECK_FALSE(
      java_regex_match(pattern, "ab", python_regex_match_kindt::FULLMATCH)
        .has_value());
  }
}

TEST_CASE(
  "Java patterns that Pattern accepts may be translated",
  "[core][solvers][strings][python_regex_to_smt]")
{
  for(const char *pattern : {"a+b", "[a-z]+[0-9]+", "(ab|c)*", "a{2,3}"})
  {
    INFO("pattern=" << pattern);
    REQUIRE(java_regex_to_smt_fullmatch(pattern).has_value());
  }
}
