/*******************************************************************\

Module: COBOL expression / type formatting for traces

Author: Kiro

\*******************************************************************/

/// \file
/// Renders CBMC expressions and types in a COBOL-flavoured syntax for
/// counterexample traces.

#ifndef CPROVER_COBOL_EXPR2COBOL_H
#define CPROVER_COBOL_EXPR2COBOL_H

#include <string>

class exprt;
class typet;
class namespacet;

std::string expr2cobol(const exprt &expr, const namespacet &ns);
std::string type2cobol(const typet &type, const namespacet &ns);

#endif // CPROVER_COBOL_EXPR2COBOL_H
