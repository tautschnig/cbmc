// Quick smoke test for the JML parser.
// Compile: g++ -std=c++17 -I src -I jbmc/src -o /tmp/jml_test
//   jbmc/src/java_bytecode/jml/jml_tokenizer.cpp
//   jbmc/src/java_bytecode/jml/jml_parser.cpp
//   jbmc/src/java_bytecode/jml/jml_parser_test.cpp
//   <link against util, etc.>
// For now, just test tokenizer + parser structure.

#include "jml_tokenizer.h"
#include "jml_parser.h"

#include <util/symbol_table.h>

#include <cassert>
#include <iostream>

int main()
{
  // Test tokenizer
  {
    auto tokens = jml_tokenize("\\old(x) + 1 >= 0");
    assert(tokens.size() == 9); // \old ( x ) + 1 >= 0 END
    assert(tokens[0].kind == jml_token_kindt::JML_OLD);
    assert(tokens[1].kind == jml_token_kindt::LPAREN);
    assert(tokens[2].kind == jml_token_kindt::IDENTIFIER);
    assert(tokens[2].text == "x");
    assert(tokens[3].kind == jml_token_kindt::RPAREN);
    assert(tokens[4].kind == jml_token_kindt::PLUS);
    assert(tokens[5].kind == jml_token_kindt::INTEGER_LITERAL);
    assert(tokens[6].kind == jml_token_kindt::GE);
    assert(tokens[7].kind == jml_token_kindt::INTEGER_LITERAL);
    assert(tokens[8].kind == jml_token_kindt::END_OF_INPUT);
    std::cout << "Tokenizer: PASS (9 tokens for '\\old(x) + 1 >= 0')\n";
  }

  // Test implies
  {
    auto tokens = jml_tokenize("a ==> b");
    assert(tokens.size() == 4); // a ==> b END
    assert(tokens[0].kind == jml_token_kindt::IDENTIFIER);
    assert(tokens[1].kind == jml_token_kindt::IMPLIES);
    assert(tokens[2].kind == jml_token_kindt::IDENTIFIER);
    assert(tokens[3].kind == jml_token_kindt::END_OF_INPUT);
    std::cout << "Tokenizer: PASS (implies)\n";
  }

  // Test equiv
  {
    auto tokens = jml_tokenize("a <==> b");
    assert(tokens.size() == 4);
    assert(tokens[1].kind == jml_token_kindt::EQUIV);
    std::cout << "Tokenizer: PASS (equiv)\n";
  }

  // Test parser
  {
    symbol_tablet st;
    auto result = jml_parse_expression(
      "x + 1 >= 0", "java::Foo.bar:()V", "java::Foo", st);
    assert(result.success);
    assert(result.expr.id() == ID_ge);
    std::cout << "Parser: PASS (x + 1 >= 0 → ge_exprt)\n";
  }

  // Test \old
  {
    symbol_tablet st;
    auto result = jml_parse_expression(
      "\\old(counter) + 1", "java::Foo.bar:()V", "java::Foo", st);
    assert(result.success);
    assert(result.expr.id() == ID_plus);
    assert(result.expr.operands()[0].id() == ID_old);
    std::cout << "Parser: PASS (\\old(counter) + 1 → plus(old(...), 1))\n";
  }

  // Test \result
  {
    symbol_tablet st;
    auto result = jml_parse_expression(
      "\\result > 0", "java::Foo.bar:()I", "java::Foo", st);
    assert(result.success);
    assert(result.expr.id() == ID_gt);
    assert(result.expr.operands()[0].id() == ID_symbol);
    std::cout << "Parser: PASS (\\result > 0)\n";
  }

  // Test implies
  {
    symbol_tablet st;
    auto result = jml_parse_expression(
      "x != null ==> x.length > 0",
      "java::Foo.bar:()V", "java::Foo", st);
    assert(result.success);
    // a ==> b is lowered to !a || b
    assert(result.expr.id() == ID_or);
    std::cout << "Parser: PASS (x != null ==> x.length > 0)\n";
  }

  // Test clause parsing
  {
    symbol_tablet st;
    auto clause = jml_parse_clause(
      "requires x > 0;", "java::Foo.bar:(I)V", "java::Foo", st);
    assert(clause.kind == jml_clauset::kindt::REQUIRES);
    assert(clause.expr.id() == ID_gt);
    std::cout << "Clause: PASS (requires x > 0)\n";
  }

  // Test ensures with \old
  {
    symbol_tablet st;
    auto clause = jml_parse_clause(
      "ensures counter == \\old(counter) + 1;",
      "java::Foo.bump:()V", "java::Foo", st);
    assert(clause.kind == jml_clauset::kindt::ENSURES);
    assert(clause.expr.id() == ID_equal);
    std::cout << "Clause: PASS (ensures counter == \\old(counter) + 1)\n";
  }

  // Test assignable
  {
    symbol_tablet st;
    auto clause = jml_parse_clause(
      "assignable \\nothing;", "java::Foo.bar:()V", "java::Foo", st);
    assert(clause.kind == jml_clauset::kindt::ASSIGNABLE);
    assert(clause.expr.id() == "jml_nothing");
    std::cout << "Clause: PASS (assignable \\nothing)\n";
  }

  // Test forall
  {
    symbol_tablet st;
    auto result = jml_parse_expression(
      "\\forall int i; 0 <= i && i < 10; i >= 0",
      "java::Foo.bar:()V", "java::Foo", st);
    assert(result.success);
    assert(result.expr.id() == ID_forall);
    std::cout << "Parser: PASS (\\forall int i; ...)\n";
  }

  std::cout << "\nAll JML parser tests PASSED.\n";
  return 0;
}
