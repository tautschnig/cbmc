/// \file
/// Convert CBMC expressions and types to TypeScript syntax for trace output

#ifndef CPROVER_TYPESCRIPT_EXPR2TYPESCRIPT_H
#define CPROVER_TYPESCRIPT_EXPR2TYPESCRIPT_H

#include <string>

class exprt;
class namespacet;
class typet;

std::string expr2typescript(const exprt &expr, const namespacet &ns);
std::string type2typescript(const typet &type, const namespacet &ns);

#endif // CPROVER_TYPESCRIPT_EXPR2TYPESCRIPT_H
