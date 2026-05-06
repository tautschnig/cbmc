/// \file
/// Convert CBMC expressions and types to TypeScript syntax for trace output

#include "expr2typescript.h"

#include <util/arith_tools.h>
#include <util/bitvector_types.h>
#include <util/ieee_float.h>
#include <util/namespace.h>
#include <util/pointer_expr.h>
#include <util/std_expr.h>
#include <util/std_types.h>

#include "typescript_types.h"

#include <cmath>

std::string type2typescript(const typet &type, const namespacet &ns)
{
  if(type.id() == ID_floatbv)
    return "number";
  if(type.id() == ID_bool)
    return "boolean";
  if(type.id() == ID_empty)
    return "void";
  if(type.id() == ID_signedbv || type.id() == ID_unsignedbv)
    return "number";
  if(type.id() == ID_pointer)
    return type2typescript(to_pointer_type(type).base_type(), ns);
  if(is_typescript_string_type(type))
    return "string";
  if(type.id() == ID_struct)
  {
    const auto &st = to_struct_type(type);
    std::string tag = id2string(st.get_tag());
    if(tag.substr(0, 17) == "typescript_class_")
      return tag.substr(17);
    if(tag == "typescript_array")
      return "Array";
    return tag.empty() ? "object" : tag;
  }
  if(type.id() == ID_array)
    return "Array";
  return id2string(type.id());
}

std::string expr2typescript(const exprt &expr, const namespacet &ns)
{
  if(expr.id() == ID_constant)
  {
    const auto &constant = to_constant_expr(expr);
    if(constant.type().id() == ID_bool)
      return constant.is_true() ? "true" : "false";
    if(constant.type().id() == ID_floatbv)
    {
      ieee_floatt fv{
        ieee_float_spect::double_precision(),
        ieee_floatt::rounding_modet::ROUND_TO_EVEN};
      fv.from_expr(constant);
      if(fv.is_NaN())
        return "NaN";
      if(fv.is_infinity())
        return fv.get_sign() ? "-Infinity" : "Infinity";
      double d = std::stod(fv.to_ansi_c_string());
      if(d == std::floor(d) && std::abs(d) < 1e15)
        return std::to_string(static_cast<long long>(d));
      return fv.to_ansi_c_string();
    }
    if(
      constant.type().id() == ID_signedbv ||
      constant.type().id() == ID_unsignedbv)
    {
      mp_integer val;
      if(!to_integer(constant, val))
        return integer2string(val);
    }
  }

  if(expr.id() == ID_symbol)
  {
    std::string id = id2string(to_symbol_expr(expr).get_identifier());
    if(id.substr(0, 13) == "typescript::")
      id = id.substr(13);
    auto pos = id.rfind("::");
    if(pos != std::string::npos)
      id = id.substr(pos + 2);
    return id;
  }

  if(expr.id() == ID_member)
    return expr2typescript(to_member_expr(expr).compound(), ns) + "." +
           id2string(to_member_expr(expr).get_component_name());

  // IEEE float operations
  if(expr.id() == ID_floatbv_plus)
    return expr2typescript(expr.operands()[0], ns) + " + " +
           expr2typescript(expr.operands()[1], ns);
  if(expr.id() == ID_floatbv_minus)
    return expr2typescript(expr.operands()[0], ns) + " - " +
           expr2typescript(expr.operands()[1], ns);
  if(expr.id() == ID_floatbv_mult)
    return expr2typescript(expr.operands()[0], ns) + " * " +
           expr2typescript(expr.operands()[1], ns);
  if(expr.id() == ID_floatbv_div)
    return expr2typescript(expr.operands()[0], ns) + " / " +
           expr2typescript(expr.operands()[1], ns);

  // Integer arithmetic
  if(expr.id() == ID_plus)
    return expr2typescript(expr.operands()[0], ns) + " + " +
           expr2typescript(expr.operands()[1], ns);
  if(expr.id() == ID_minus)
    return expr2typescript(expr.operands()[0], ns) + " - " +
           expr2typescript(expr.operands()[1], ns);
  if(expr.id() == ID_mult)
    return expr2typescript(expr.operands()[0], ns) + " * " +
           expr2typescript(expr.operands()[1], ns);
  if(expr.id() == ID_div)
    return expr2typescript(expr.operands()[0], ns) + " / " +
           expr2typescript(expr.operands()[1], ns);

  // Comparisons
  if(expr.id() == ID_ieee_float_equal)
    return expr2typescript(expr.operands()[0], ns) +
           " === " + expr2typescript(expr.operands()[1], ns);
  if(expr.id() == ID_ieee_float_notequal)
    return expr2typescript(expr.operands()[0], ns) +
           " !== " + expr2typescript(expr.operands()[1], ns);
  if(expr.id() == ID_equal)
    return expr2typescript(expr.operands()[0], ns) +
           " === " + expr2typescript(expr.operands()[1], ns);
  if(expr.id() == ID_notequal)
    return expr2typescript(expr.operands()[0], ns) +
           " !== " + expr2typescript(expr.operands()[1], ns);
  if(expr.id() == ID_lt)
    return expr2typescript(expr.operands()[0], ns) + " < " +
           expr2typescript(expr.operands()[1], ns);
  if(expr.id() == ID_le)
    return expr2typescript(expr.operands()[0], ns) +
           " <= " + expr2typescript(expr.operands()[1], ns);
  if(expr.id() == ID_gt)
    return expr2typescript(expr.operands()[0], ns) + " > " +
           expr2typescript(expr.operands()[1], ns);
  if(expr.id() == ID_ge)
    return expr2typescript(expr.operands()[0], ns) +
           " >= " + expr2typescript(expr.operands()[1], ns);

  // Logical
  if(expr.id() == ID_not)
    return "!" + expr2typescript(expr.operands()[0], ns);
  if(expr.id() == ID_and)
    return expr2typescript(expr.operands()[0], ns) + " && " +
           expr2typescript(expr.operands()[1], ns);
  if(expr.id() == ID_or)
    return expr2typescript(expr.operands()[0], ns) + " || " +
           expr2typescript(expr.operands()[1], ns);

  if(expr.id() == ID_unary_minus)
    return "-" + expr2typescript(expr.operands()[0], ns);

  if(expr.id() == ID_typecast)
    return expr2typescript(to_typecast_expr(expr).op(), ns);

  if(expr.id() == ID_index)
    return expr2typescript(to_index_expr(expr).array(), ns) + "[" +
           expr2typescript(to_index_expr(expr).index(), ns) + "]";

  if(expr.id() == ID_if)
    return expr2typescript(to_if_expr(expr).cond(), ns) + " ? " +
           expr2typescript(to_if_expr(expr).true_case(), ns) + " : " +
           expr2typescript(to_if_expr(expr).false_case(), ns);

  if(expr.id() == ID_dereference)
    return expr2typescript(to_dereference_expr(expr).pointer(), ns);
  if(expr.id() == ID_address_of)
    return expr2typescript(to_address_of_expr(expr).object(), ns);

  // Fallback
  return "(" + id2string(expr.id()) + ")";
}
