/// \file
/// Convert CBMC expressions and types to Python syntax for trace output

#ifndef CPROVER_PYTHON_EXPR2PYTHON_H
#define CPROVER_PYTHON_EXPR2PYTHON_H

#include <string>

class exprt;
class namespacet;
class typet;

std::string expr2python(const exprt &expr, const namespacet &ns);
std::string type2python(const typet &type, const namespacet &ns);

#endif // CPROVER_PYTHON_EXPR2PYTHON_H
