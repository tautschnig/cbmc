/*******************************************************************\

Module:

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/


#ifndef CPROVER_UTIL_REPLACE_EXPR_H
#define CPROVER_UTIL_REPLACE_EXPR_H

//
// true: did nothing
// false: replaced something
//

#include "expr.h"

#include <unordered_map>

typedef std::unordered_map<exprt, exprt, irep_hash> replace_mapt;

bool replace_expr(const exprt &what, const exprt &by, exprt &dest);
bool replace_expr(const replace_mapt &what, exprt &dest);

/// As replace_expr, visiting each node of the DAG of \p dest once rather
/// than each node of its tree, which can be exponentially larger where
/// subexpressions are shared, at the cost of a table of the visited nodes.
bool replace_expr_shared(const exprt &what, const exprt &by, exprt &dest);
bool replace_expr_shared(const replace_mapt &what, exprt &dest);

#endif // CPROVER_UTIL_REPLACE_EXPR_H
