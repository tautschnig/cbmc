/*******************************************************************\

Module: Go front-end: expressions and types as Go source

Author: Michael Tautschnig

\*******************************************************************/

/// \file
/// Printing CBMC expressions and types in Go syntax.

#include "expr2go.h"

#include <util/arith_tools.h>
#include <util/format_expr.h>
#include <util/format_type.h>
#include <util/namespace.h>
#include <util/pointer_expr.h>
#include <util/std_expr.h>
#include <util/symbol.h>

#include "go_types.h"

#include <sstream>

std::string type2go(const typet &type, const namespacet &)
{
  const irep_idt &go_name = type.get(ID_C_go_type);
  if(!go_name.empty())
    return id2string(go_name);
  if(type.id() == ID_bool)
    return "bool";
  if(type.id() == ID_signedbv)
    return "int" + type.get_string(ID_width);
  if(type.id() == ID_unsignedbv)
    return "uint" + type.get_string(ID_width);
  std::ostringstream out;
  out << format(type);
  return out.str();
}

static const char *binary_operator(const irep_idt &id)
{
  if(id == ID_plus)
    return "+";
  if(id == ID_minus)
    return "-";
  if(id == ID_mult)
    return "*";
  if(id == ID_div)
    return "/";
  if(id == ID_mod)
    return "%";
  if(id == ID_bitand)
    return "&";
  if(id == ID_bitor)
    return "|";
  if(id == ID_bitxor)
    return "^";
  if(id == ID_shl)
    return "<<";
  if(id == ID_ashr || id == ID_lshr)
    return ">>";
  if(id == ID_equal)
    return "==";
  if(id == ID_notequal)
    return "!=";
  if(id == ID_lt)
    return "<";
  if(id == ID_le)
    return "<=";
  if(id == ID_gt)
    return ">";
  if(id == ID_ge)
    return ">=";
  if(id == ID_and)
    return "&&";
  if(id == ID_or)
    return "||";
  return nullptr;
}

std::string expr2go(const exprt &expr, const namespacet &ns)
{
  if(expr.is_constant())
  {
    if(expr.is_true())
      return "true";
    if(expr.is_false())
      return "false";
    if(auto value = numeric_cast<mp_integer>(expr))
      return integer2string(*value);
  }
  else if(expr.id() == ID_symbol)
  {
    const symbolt *symbol;
    if(!ns.lookup(to_symbol_expr(expr).get_identifier(), symbol))
      return id2string(symbol->base_name);
    return id2string(to_symbol_expr(expr).get_identifier());
  }
  else if(expr.id() == ID_typecast)
  {
    // Go1.27 §Conversions: T(x)
    return type2go(expr.type(), ns) + "(" +
           expr2go(to_typecast_expr(expr).op(), ns) + ")";
  }
  else if(expr.id() == ID_unary_minus)
    return "-" + expr2go(to_unary_minus_expr(expr).op(), ns);
  else if(expr.id() == ID_bitnot)
    return "^" + expr2go(to_unary_expr(expr).op(), ns);
  else if(expr.id() == ID_not)
    return "!" + expr2go(to_not_expr(expr).op(), ns);
  else if(expr.operands().size() == 2)
  {
    if(const char *op = binary_operator(expr.id()))
      return "(" + expr2go(expr.operands()[0], ns) + " " + op + " " +
             expr2go(expr.operands()[1], ns) + ")";
  }

  std::ostringstream out;
  out << format(expr);
  return out.str();
}
