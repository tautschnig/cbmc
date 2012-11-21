/*******************************************************************\

Module:

Author: Michael Tautschnig, michael.tautschnig@gmail.com

\*******************************************************************/

#ifndef CPROVER_SIMPLIFY_PARSE_H
#define CPROVER_SIMPLIFY_PARSE_H

#include "expr.h"

class namespacet;

class simplify_parset
{
  public:
    explicit simplify_parset()
    {
      simplified.make_nil();
    }

    bool parse(const exprt &expr);

    const exprt& get_simplified() const
    {
      return simplified;
    }

  private:
    exprt simplified;
};

#endif

