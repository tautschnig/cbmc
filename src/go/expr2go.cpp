/*******************************************************************\

Module: Go front-end: expressions and types as Go source

Author: Michael Tautschnig

\*******************************************************************/

/// \file
/// Printing CBMC expressions and types in Go syntax.

#include "expr2go.h"

#include <util/arith_tools.h>
#include <util/expr_util.h>
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
  if(type.id() == ID_bool || type.id() == ID_c_bool)
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
    // Go1.27 §Pointer_types: "The value of an uninitialized pointer is nil."
    if(
      expr.type().id() == ID_pointer &&
      to_constant_expr(expr).is_null_pointer())
      return "nil";
    if(expr.type().id() == ID_c_bool)
      return to_constant_expr(expr).is_zero() ? "false" : "true";
    if(auto value = numeric_cast<mp_integer>(expr))
      return integer2string(*value);
  }
  else if(expr.id() == ID_annotated_pointer_constant)
  {
    // Pointer values in traces: print the pointer they stand for.
    const exprt &pointer =
      to_annotated_pointer_constant_expr(expr).symbolic_pointer();
    if(pointer.is_constant() && to_constant_expr(pointer).is_null_pointer())
      return "nil";
    return expr2go(pointer, ns);
  }
  else if(expr.id() == ID_address_of)
    return "&" + expr2go(to_address_of_expr(expr).object(), ns);
  else if(expr.id() == ID_dereference)
    return "*" + expr2go(to_dereference_expr(expr).pointer(), ns);
  else if(expr.id() == ID_member)
  {
    // Fields are named after their Go names (blank ones are "_$N").
    const member_exprt &member = to_member_expr(expr);
    const exprt &compound = member.compound();
    const std::string base =
      compound.id() == ID_dereference
        ? expr2go(to_dereference_expr(compound).pointer(), ns)
        : expr2go(compound, ns);
    return base + "." + id2string(member.get_component_name());
  }
  else if(expr.id() == ID_index)
  {
    const index_exprt &index = to_index_expr(expr);
    return expr2go(index.array(), ns) + "[" + expr2go(index.index(), ns) + "]";
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
