/*******************************************************************\

Module: COBOL expression / type formatting for traces

Author: Kiro

\*******************************************************************/

/// \file
/// Renders CBMC expressions and types in a COBOL-flavoured syntax.

#include "expr2cobol.h"

#include <util/arith_tools.h>
#include <util/bitvector_types.h>
#include <util/namespace.h>
#include <util/std_expr.h>
#include <util/symbol.h>

static std::string expr2cobol_rec(const exprt &expr, const namespacet &ns)
{
  if(expr.is_constant())
  {
    const auto &c = to_constant_expr(expr);
    if(expr.type().id() == ID_signedbv || expr.type().id() == ID_unsignedbv)
    {
      const auto i = numeric_cast<mp_integer>(c);
      if(i.has_value())
        return integer2string(*i);
    }
    return id2string(c.get_value());
  }

  if(expr.id() == ID_symbol)
  {
    const irep_idt &id = to_symbol_expr(expr).get_identifier();
    // Strip the "cobol::PROGRAM::" qualification for readability.
    const std::string s = id2string(id);
    const std::size_t pos = s.rfind("::");
    if(pos != std::string::npos)
      return s.substr(pos + 2);
    return s;
  }

  const auto binary = [&](const std::string &op)
  {
    return "(" + expr2cobol_rec(expr.operands()[0], ns) + " " + op + " " +
           expr2cobol_rec(expr.operands()[1], ns) + ")";
  };

  if(expr.operands().size() == 2)
  {
    if(expr.id() == ID_plus)
      return binary("+");
    if(expr.id() == ID_minus)
      return binary("-");
    if(expr.id() == ID_mult)
      return binary("*");
    if(expr.id() == ID_div)
      return binary("/");
    if(expr.id() == ID_equal)
      return binary("=");
    if(expr.id() == ID_notequal)
      return binary("<>");
    if(expr.id() == ID_lt)
      return binary("<");
    if(expr.id() == ID_gt)
      return binary(">");
    if(expr.id() == ID_le)
      return binary("<=");
    if(expr.id() == ID_ge)
      return binary(">=");
    if(expr.id() == ID_and)
      return binary("AND");
    if(expr.id() == ID_or)
      return binary("OR");
  }

  if(expr.id() == ID_not && expr.operands().size() == 1)
    return "(NOT " + expr2cobol_rec(expr.operands()[0], ns) + ")";

  if(expr.id() == ID_typecast && expr.operands().size() == 1)
    return expr2cobol_rec(expr.operands()[0], ns);

  return id2string(expr.id());
}

std::string expr2cobol(const exprt &expr, const namespacet &ns)
{
  return expr2cobol_rec(expr, ns);
}

std::string type2cobol(const typet &type, const namespacet &ns)
{
  if(type.id() == ID_signedbv || type.id() == ID_unsignedbv)
    return "NUMERIC";
  if(type.id() == ID_array)
    return "ALPHANUMERIC";
  if(type.id() == ID_struct || type.id() == ID_struct_tag)
    return "GROUP";
  if(type.id() == ID_bool)
    return "CONDITION";
  if(type.id() == ID_empty)
    return "VOID";
  (void)ns;
  return id2string(type.id());
}
