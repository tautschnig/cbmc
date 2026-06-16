/*******************************************************************\

Module: Unit tests for python_regex_segment_groups in
        solvers/strings/python_regex_to_smt.h

Author: Python re support, Phase 3 (Match.group(n)).

\*******************************************************************/

#include <solvers/strings/python_regex_to_smt.h>
#include <testing-utils/use_catch.h>

// Convenience: assert the pattern is rejected (sound nondet floor).
static void require_bails(const std::string &pattern)
{
  INFO("pattern: " << pattern);
  REQUIRE_FALSE(python_regex_segment_groups(pattern).has_value());
}

SCENARIO(
  "python_regex_segment_groups segments capture groups",
  "[core][solvers][strings][python_regex]")
{
  GIVEN("two digit groups separated by a literal '-'")
  {
    auto segs = python_regex_segment_groups("(\\d+)-(\\d+)");
    THEN("it yields group, literal, group")
    {
      REQUIRE(segs.has_value());
      REQUIRE(segs->size() == 3);

      REQUIRE((*segs)[0].is_group);
      REQUIRE((*segs)[0].sub_pattern == "\\d+");
      REQUIRE_FALSE((*segs)[0].literal.has_value());

      REQUIRE_FALSE((*segs)[1].is_group);
      REQUIRE((*segs)[1].literal.has_value());
      REQUIRE(*(*segs)[1].literal == "-");

      REQUIRE((*segs)[2].is_group);
      REQUIRE((*segs)[2].sub_pattern == "\\d+");
    }
  }

  GIVEN("leading/trailing literal runs around a group")
  {
    auto segs = python_regex_segment_groups("id=(\\w+);");
    THEN("the literal runs are pure-literal fragments")
    {
      REQUIRE(segs.has_value());
      REQUIRE(segs->size() == 3);
      REQUIRE_FALSE((*segs)[0].is_group);
      REQUIRE(*(*segs)[0].literal == "id=");
      REQUIRE((*segs)[1].is_group);
      REQUIRE((*segs)[1].sub_pattern == "\\w+");
      REQUIRE_FALSE((*segs)[2].is_group);
      REQUIRE(*(*segs)[2].literal == ";");
    }
  }

  GIVEN("anchors around the whole pattern")
  {
    auto segs = python_regex_segment_groups("^(\\d+)$");
    THEN("the anchors are stripped, leaving one group")
    {
      REQUIRE(segs.has_value());
      REQUIRE(segs->size() == 1);
      REQUIRE((*segs)[0].is_group);
      REQUIRE((*segs)[0].sub_pattern == "\\d+");
    }
  }

  GIVEN("adjacent groups with no separator")
  {
    auto segs = python_regex_segment_groups("(\\d+)(\\d+)");
    THEN("both groups are present (decomposition stays sound but ambiguous)")
    {
      REQUIRE(segs.has_value());
      REQUIRE(segs->size() == 2);
      REQUIRE((*segs)[0].is_group);
      REQUIRE((*segs)[1].is_group);
    }
  }

  GIVEN("a group whose body contains alternation")
  {
    auto segs = python_regex_segment_groups("(foo|bar)-(\\d+)");
    THEN("alternation inside the group is kept in its sub-pattern")
    {
      REQUIRE(segs.has_value());
      REQUIRE(segs->size() == 3);
      REQUIRE((*segs)[0].is_group);
      REQUIRE((*segs)[0].sub_pattern == "foo|bar");
    }
  }

  GIVEN("an escaped paren that is a literal, not a group")
  {
    auto segs = python_regex_segment_groups("\\((\\d+)\\)");
    THEN("the escaped parens are literal fragments")
    {
      REQUIRE(segs.has_value());
      REQUIRE(segs->size() == 3);
      REQUIRE_FALSE((*segs)[0].is_group);
      REQUIRE(*(*segs)[0].literal == "(");
      REQUIRE((*segs)[1].is_group);
      REQUIRE_FALSE((*segs)[2].is_group);
      REQUIRE(*(*segs)[2].literal == ")");
    }
  }

  GIVEN("a char class containing a paren and a separator")
  {
    auto segs = python_regex_segment_groups("([(a-z]+):(\\d+)");
    THEN("the class is not mistaken for a group boundary")
    {
      REQUIRE(segs.has_value());
      REQUIRE(segs->size() == 3);
      REQUIRE((*segs)[0].is_group);
      REQUIRE((*segs)[0].sub_pattern == "[(a-z]+");
      REQUIRE_FALSE((*segs)[1].is_group);
      REQUIRE(*(*segs)[1].literal == ":");
    }
  }
}

SCENARIO(
  "python_regex_segment_groups rejects the unsound subset",
  "[core][solvers][strings][python_regex]")
{
  // No capture group: nothing to extract.
  require_bails("\\d+");
  require_bails("abc");
  // Top-level alternation breaks the linear concatenation model.
  require_bails("(a)|(b)");
  require_bails("a|b");
  // A quantifier on a group repeats it.
  require_bails("(\\d)+");
  require_bails("(ab)*");
  require_bails("(\\d+)?");
  require_bails("(\\d){2,3}");
  // Nested groups change numbering and structure.
  require_bails("((\\d+))");
  require_bails("(a(b)c)");
  // Non-capturing / lookaround / named groups.
  require_bails("(?:\\d+)");
  require_bails("(?P<n>\\d+)");
  require_bails("(?=\\d+)");
  // Back-references and word boundaries.
  require_bails("(\\d+)\\1");
  require_bails("\\b(\\d+)\\b");
  // Mid-pattern anchor.
  require_bails("(\\d+)$x");
}
