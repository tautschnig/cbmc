/*******************************************************************\

Module: Generates string constraints for functions generating strings
        from other types, in particular int, long, float, double, char, bool

Author: Romain Brenguier, romain.brenguier@diffblue.com

\*******************************************************************/

/// \file
/// Generates string constraints for functions generating strings from other
/// types, in particular int, long, float, double, char, bool

#include <util/arith_tools.h>
#include <util/ieee_float.h>
#include <util/mathematical_expr.h>
#include <util/simplify_expr.h>

#include "string_constraint_generator.h"

/// String representation of a floating-point value, in \p format (see
/// format_float).
///
/// The previous axioms gave five fractional digits and the integer part
/// modulo 10^5, single-precision digits for doubles in scientific notation,
/// and contradictions for negative values, so they proved wrong results
/// and made paths infeasible. The representation of a value is now known
/// exactly where the value is constant. Otherwise the result is left open
/// and recorded in float_conversions: the refinement compares it with
/// format_float of the value in each model, and excludes the models where
/// they differ.
/// \param f: function application with arguments the length and the
///   characters of the result, and the value
/// \param format: the representation to give
/// \return code 0
std::pair<exprt, string_constraintst>
string_constraint_generatort::add_axioms_for_string_of_float(
  const function_application_exprt &f,
  float_formatt format)
{
  PRECONDITION(f.arguments().size() == 3);
  const array_string_exprt res =
    array_pool.find(f.arguments()[1], f.arguments()[0]);
  const exprt &value = f.arguments()[2];
  const exprt simplified = simplify_expr(value, ns);
  if(simplified.is_constant() && simplified.type().id() == ID_floatbv)
  {
    const ieee_float_valuet constant{to_constant_expr(simplified)};
    const bool single = constant.spec == ieee_float_spect::single_precision();
    if(single || constant.spec == ieee_float_spect::double_precision())
    {
      const double d = single ? constant.to_float() : constant.to_double();
      return add_axioms_for_constant(res, format_float(d, single, format));
    }
  }
  float_conversions.push_back({value, res, format});
  return {from_integer(0, get_return_code_type()), {}};
}
