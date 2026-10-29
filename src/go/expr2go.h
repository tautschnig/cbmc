/*******************************************************************\

Module: Go front-end: expressions and types as Go source

Author: Michael Tautschnig

\*******************************************************************/

/// \file
/// Printing CBMC expressions and types in Go syntax (for traces and
/// --show-goto-functions).

#ifndef CPROVER_GO_EXPR2GO_H
#define CPROVER_GO_EXPR2GO_H

#include <string>

class exprt;
class namespacet;
class typet;

std::string expr2go(const exprt &expr, const namespacet &ns);
std::string type2go(const typet &type, const namespacet &ns);

#endif // CPROVER_GO_EXPR2GO_H
