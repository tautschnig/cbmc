/*******************************************************************\

Module:

Author: Michael Tautschnig, michael.tautschnig@gmail.com

\*******************************************************************/

#ifndef CPROVER_SIMPLIFY_UNPARSE_H
#define CPROVER_SIMPLIFY_UNPARSE_H

#include <stack>

class exprt;

#define YYSTYPE exprt const*

class simplify_unparset
{
  public:
    explicit simplify_unparset(const exprt &expr);

    int yylex();
    void abort_expr();

  private:
    enum markert {
      BEGIN_OPERANDS,
      OPERAND,
      END_OPERANDS
    };
    std::stack<std::pair<const exprt*, markert> > stack;

    int expr_to_tok(const exprt& expr) const;
};

#endif

