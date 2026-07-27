/// \file
/// Convert CBMC expressions and types to Python syntax for trace output

#include "expr2python.h"

#include <util/arith_tools.h>
#include <util/bitvector_types.h>
#include <util/namespace.h>
#include <util/pointer_expr.h>
#include <util/std_expr.h>
#include <util/std_types.h>

#include "python_types.h"

std::string type2python(const typet &type, const namespacet &ns)
{
  if(type.id() == ID_signedbv)
    return "int";
  else if(type.id() == ID_floatbv)
    return "float";
  else if(type.id() == ID_bool)
    return "bool";
  else if(type.id() == ID_empty)
    return "None";
  else if(type.id() == ID_pointer)
    return type2python(to_pointer_type(type).base_type(), ns);
  else if(type.id() == ID_struct)
  {
    const auto &st = to_struct_type(type);
    std::string tag = id2string(st.get_tag());
    if(tag.substr(0, 13) == "python_class_")
      return tag.substr(13);
    if(is_python_string_type(type))
      return "str";
    if(is_python_list_type(type))
      return "list";
    if(is_python_tuple_type(type))
      return "tuple";
    return tag;
  }
  return id2string(type.id());
}

std::string expr2python(const exprt &expr, const namespacet &ns)
{
  if(expr.id() == ID_constant)
  {
    const auto &constant = to_constant_expr(expr);

    if(constant.type().id() == ID_bool)
      return constant.is_true() ? "True" : "False";

    if(constant.type().id() == ID_signedbv)
    {
      mp_integer val;
      if(!to_integer(constant, val))
        return integer2string(val);
    }

    if(constant.type().id() == ID_floatbv)
    {
      return constant.get_value().empty() ? "0.0"
                                          : id2string(constant.get_value());
    }

    // Native SMT-String value (Plan A): the constant's value is the string
    // content; render it as a Python string literal.
    if(constant.type().id() == ID_string)
      return "'" + id2string(constant.get_value()) + "'";
  }

  if(expr.id() == ID_symbol)
  {
    const auto &sym = to_symbol_expr(expr);
    std::string id = id2string(sym.get_identifier());
    // Strip python:: prefix and function scope
    if(id.substr(0, 8) == "python::")
      id = id.substr(8);
    auto pos = id.rfind("::");
    if(pos != std::string::npos)
      id = id.substr(pos + 2);
    return id;
  }

  if(expr.id() == ID_member)
  {
    const auto &member = to_member_expr(expr);
    return expr2python(member.compound(), ns) + "." +
           id2string(member.get_component_name());
  }

  if(expr.id() == ID_plus)
    return expr2python(expr.operands()[0], ns) + " + " +
           expr2python(expr.operands()[1], ns);
  if(expr.id() == ID_minus)
    return expr2python(expr.operands()[0], ns) + " - " +
           expr2python(expr.operands()[1], ns);
  if(expr.id() == ID_mult)
    return expr2python(expr.operands()[0], ns) + " * " +
           expr2python(expr.operands()[1], ns);
  if(expr.id() == ID_div)
    return expr2python(expr.operands()[0], ns) + " // " +
           expr2python(expr.operands()[1], ns);

  if(expr.id() == ID_equal)
    return expr2python(expr.operands()[0], ns) +
           " == " + expr2python(expr.operands()[1], ns);
  if(expr.id() == ID_notequal)
    return expr2python(expr.operands()[0], ns) +
           " != " + expr2python(expr.operands()[1], ns);
  if(expr.id() == ID_lt)
    return expr2python(expr.operands()[0], ns) + " < " +
           expr2python(expr.operands()[1], ns);
  if(expr.id() == ID_le)
    return expr2python(expr.operands()[0], ns) +
           " <= " + expr2python(expr.operands()[1], ns);
  if(expr.id() == ID_gt)
    return expr2python(expr.operands()[0], ns) + " > " +
           expr2python(expr.operands()[1], ns);
  if(expr.id() == ID_ge)
    return expr2python(expr.operands()[0], ns) +
           " >= " + expr2python(expr.operands()[1], ns);

  if(expr.id() == ID_not)
    return "not " + expr2python(expr.operands()[0], ns);
  if(expr.id() == ID_and)
    return expr2python(expr.operands()[0], ns) + " and " +
           expr2python(expr.operands()[1], ns);
  if(expr.id() == ID_or)
    return expr2python(expr.operands()[0], ns) + " or " +
           expr2python(expr.operands()[1], ns);

  if(expr.id() == ID_unary_minus)
    return "-" + expr2python(expr.operands()[0], ns);

  if(expr.id() == ID_typecast)
    return expr2python(to_typecast_expr(expr).op(), ns);

  if(expr.id() == ID_struct)
  {
    if(is_python_string_type(expr.type()))
    {
      // Try to extract string content from the data array
      const auto &struct_expr = to_struct_expr(expr);
      if(struct_expr.operands().size() >= 2)
      {
        const auto &length_expr = struct_expr.operands()[0];
        mp_integer len;
        if(
          length_expr.is_constant() &&
          !to_integer(to_constant_expr(length_expr), len))
        {
          const auto &data = struct_expr.operands()[1];
          std::string result = "\"";
          for(mp_integer i = 0; i < len; ++i)
          {
            if(i < data.operands().size())
            {
              const auto &char_expr = data.operands()[i.to_long()];
              mp_integer ch;
              if(
                char_expr.is_constant() &&
                !to_integer(to_constant_expr(char_expr), ch))
                result += static_cast<char>(ch.to_long());
            }
          }
          result += "\"";
          return result;
        }
      }
    }
  }

  if(expr.id() == ID_dereference)
    return expr2python(to_dereference_expr(expr).pointer(), ns);

  if(expr.id() == ID_address_of)
    return expr2python(to_address_of_expr(expr).object(), ns);

  // Fallback
  return "(" + id2string(expr.id()) + ")";
}
