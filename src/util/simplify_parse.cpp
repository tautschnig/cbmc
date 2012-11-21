/*******************************************************************\

Module:

Author: Michael Tautschnig, michael.tautschnig@gmail.com

\*******************************************************************/

#include <list>

#include "simplify_unparse.h"
#include "simplify_parse.h"

int yysimplifyparse(
    simplify_unparset &lexer,
    const exprt& expr,
    exprt& dest,
    std::list<exprt> &temps);

/*******************************************************************\

Function: simplify_parset::parse

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool simplify_parset::parse(const exprt& expr)
{
  simplify_unparset lexer(expr);
  std::list<exprt> temps;

  yysimplifyparse(lexer, expr, simplified, temps);

  return simplified.is_nil();
}

