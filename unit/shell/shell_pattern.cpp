/// \file
/// Unit tests for compiling shell patterns.

#include <shell/shell_pattern.h>
#include <testing-utils/use_catch.h>

namespace
{
shell_wordt literal(const std::string &text)
{
  shell_word_partt part;
  part.kind = shell_word_partt::kindt::LITERAL;
  part.text = text;
  return {part};
}

std::optional<shell_patternt>
compile(const std::string &text, shell_dialectt dialect = shell_dialectt::POSIX)
{
  std::string error;
  return compile_shell_pattern(literal(text), dialect, error);
}

using kindt = shell_pattern_elementt::kindt;
} // namespace

TEST_CASE("shell pattern: elements", "[shell]")
{
  auto p = compile("a*?\\*");
  REQUIRE(p.has_value());
  REQUIRE(p->elements.size() == 4);
  REQUIRE(p->elements[0].kind == kindt::CHARACTER);
  REQUIRE(p->elements[1].kind == kindt::STAR);
  REQUIRE(p->elements[2].kind == kindt::ANY);
  REQUIRE(p->elements[3].kind == kindt::CHARACTER);
  REQUIRE(p->elements[3].character == '*');
  REQUIRE_FALSE(p->is_literal());
  REQUIRE(compile("a\\*b")->is_literal());
  // ** is *.
  REQUIRE(compile("**")->elements.size() == 1);
}

TEST_CASE("shell pattern: bracket expressions", "[shell]")
{
  auto set = compile("[a-c]")->elements.at(0);
  REQUIRE(set.kind == kindt::SET);
  REQUIRE(set.set.test('b'));
  REQUIRE_FALSE(set.set.test('d'));
  auto negated = compile("[!a]")->elements.at(0);
  REQUIRE_FALSE(negated.set.test('a'));
  REQUIRE(negated.set.test('b'));
  // ] first is literal, - last is literal.
  REQUIRE(compile("[]a]")->elements.at(0).set.test(']'));
  REQUIRE(compile("[a-]")->elements.at(0).set.test('-'));
  REQUIRE(compile("[[:digit:]]")->elements.at(0).set.test('5'));
  // Without a closing ], [ is literal.
  REQUIRE(compile("a[")->is_literal());
  // [^...] is unspecified in POSIX; bash reads it as negation.
  REQUIRE_FALSE(compile("[^a]").has_value());
  REQUIRE_FALSE(
    compile("[^a]", shell_dialectt::BASH)->elements.at(0).set.test('a'));
  REQUIRE_FALSE(compile("[[=a=]]").has_value());
  REQUIRE_FALSE(compile("[[:nope:]]").has_value());
}
