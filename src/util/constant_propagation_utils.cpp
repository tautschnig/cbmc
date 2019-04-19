/*******************************************************************\

Module: Constant propagation -- utility functions

Author: Peter Schrammel

\*******************************************************************/

/// \file
/// Constant propagation utilities

#include "constant_propagation_utils.h"

#include "arith_tools.h"
#include "expr_util.h"
#include "format_expr.h"
#include "ieee_float.h"
#include "mathematical_types.h"
#include "namespace.h"
#include "simplify_expr.h"
#include "std_expr.h"
#include "symbol.h"

#include <array>

class constant_propagator_is_constantt : public is_constantt
{
public:
  explicit constant_propagator_is_constantt(
    const replace_symbolt &replace_const)
    : replace_const(replace_const)
  {
  }

  bool is_constant(const irep_idt &id) const
  {
    return replace_const.replaces_symbol(id);
  }

protected:
  bool is_constant(const exprt &expr) const override
  {
    if(expr.id() == ID_symbol)
      return is_constant(to_symbol_expr(expr).get_identifier());

    return is_constantt::is_constant(expr);
  }

  const replace_symbolt &replace_const;
};

bool constant_valuest::is_constant(const exprt &expr) const
{
  return constant_propagator_is_constantt(replace_const)(expr);
}

bool constant_valuest::is_constant(const irep_idt &id) const
{
  return constant_propagator_is_constantt(replace_const).is_constant(id);
}

/// Do not call this when iterating over replace_const.expr_map!
bool constant_valuest::set_to_top(const symbol_exprt &symbol_expr)
{
  const auto n_erased = replace_const.erase(symbol_expr.get_identifier());

  INVARIANT(
    n_erased == 0 || !is_bottom, "bottom should have no elements at all");

  return n_erased > 0;
}

void constant_valuest::set_dirty_to_top(
  const std::unordered_set<irep_idt> &dirty_set,
  const namespacet &ns)
{
  typedef replace_symbolt::expr_mapt expr_mapt;
  expr_mapt &expr_map = replace_const.get_expr_map();

  for(expr_mapt::iterator it = expr_map.begin(); it != expr_map.end();)
  {
    const irep_idt id = it->first;

    const symbolt &symbol = ns.lookup(id);

    if(
      (symbol.is_static_lifetime || dirty_set.count(id) != 0) &&
      !symbol.type.get_bool(ID_C_constant))
    {
      it = replace_const.erase(it);
    }
    else
      it++;
  }
}

void constant_valuest::output(std::ostream &out) const
{
  out << "const map:\n";

  if(is_bottom)
  {
    out << "  bottom\n";
    DATA_INVARIANT(
      is_empty(), "If the domain is bottom, the map must be empty");
    return;
  }

  INVARIANT(!is_bottom, "Have handled bottom");
  if(is_empty())
  {
    out << "top\n";
    return;
  }

  for(const auto &p : replace_const.get_expr_map())
  {
    out << ' ' << p.first << "=" << format(p.second) << '\n';
  }
}

bool constant_valuest::merge(const constant_valuest &src)
{
  // nothing to do
  if(src.is_bottom)
    return false;

  // just copy
  if(is_bottom)
  {
    PRECONDITION(!src.is_bottom);
    replace_const = src.replace_const; // copy
    is_bottom = false;
    return true;
  }

  INVARIANT(!is_bottom && !src.is_bottom, "Case handled");

  bool changed = false;

  // handle top
  if(src.is_empty())
  {
    // change if it was not top
    changed = !is_empty();

    set_to_top();

    return changed;
  }

  replace_symbolt::expr_mapt &expr_map = replace_const.get_expr_map();
  const replace_symbolt::expr_mapt &src_expr_map =
    src.replace_const.get_expr_map();

  // remove those that are
  // - different in src
  // - do not exist in src
  for(replace_symbolt::expr_mapt::iterator it = expr_map.begin();
      it != expr_map.end();)
  {
    const irep_idt id = it->first;
    const exprt &expr = it->second;

    replace_symbolt::expr_mapt::const_iterator s_it;
    s_it = src_expr_map.find(id);

    if(s_it != src_expr_map.end())
    {
      // check value
      const exprt &src_expr = s_it->second;

      if(expr != src_expr)
      {
        it = replace_const.erase(it);
        changed = true;
      }
      else
        it++;
    }
    else
    {
      it = replace_const.erase(it);
      changed = true;
    }
  }

  return changed;
}

bool constant_valuest::meet(const constant_valuest &src, const namespacet &ns)
{
  if(src.is_bottom || is_bottom)
    return false;

  bool changed = false;

  for(const auto &m : src.replace_const.get_expr_map())
  {
    replace_symbolt::expr_mapt::const_iterator c_it =
      replace_const.get_expr_map().find(m.first);

    if(c_it != replace_const.get_expr_map().end())
    {
      if(c_it->second != m.second)
      {
        set_to_bottom();
        changed = true;
        break;
      }
    }
    else
    {
      const typet &m_id_type = ns.lookup(m.first).type;
      DATA_INVARIANT(
        m_id_type == m.second.type(),
        "type of constant to be stored should match");
      set_to(symbol_exprt(m.first, m_id_type), m.second);
      changed = true;
    }
  }

  return changed;
}

static bool replace_constants_and_simplify(
  const constant_valuest &known_values,
  exprt &expr,
  const namespacet &ns)
{
  bool did_not_change_anything = true;

  // iterate constant propagation and simplification until we cannot
  // constant-propagate any further - a prime example is pointer dereferencing,
  // where constant propagation may have a value of the pointer, the simplifier
  // takes care of evaluating dereferencing, and we might then have the value of
  // the resulting symbol known to constant propagation and thus replace the
  // dereferenced expression by a constant
  while(!known_values.replace(expr))
  {
    did_not_change_anything = false;
    simplify(expr, ns);
  }

  // even if we haven't been able to constant-propagate anything, run the
  // simplifier on the expression
  if(did_not_change_anything)
    did_not_change_anything &= simplify(expr, ns);

  return did_not_change_anything;
}

/// Attempt to evaluate an expression in all rounding modes.
///
/// \param known_values: The constant values under which to evaluate \p expr
/// \param expr: The expression to evaluate
/// \param ns: The namespace for symbols in the expression
/// \return If the result is the same for all rounding modes, change
///   expr to that result and return false. Otherwise, return true.
static bool partial_evaluate_with_all_rounding_modes(
  const constant_valuest &known_values,
  exprt &expr,
  const namespacet &ns)
{ // NOLINTNEXTLINE (whitespace/braces)
  auto rounding_modes = std::array<ieee_floatt::rounding_modet, 4>{
    // NOLINTNEXTLINE (whitespace/braces)
    {ieee_floatt::ROUND_TO_EVEN,
     ieee_floatt::ROUND_TO_ZERO,
     ieee_floatt::ROUND_TO_MINUS_INF,
     // NOLINTNEXTLINE (whitespace/braces)
     ieee_floatt::ROUND_TO_PLUS_INF}};
  exprt first_result;
  for(std::size_t i = 0; i < rounding_modes.size(); ++i)
  {
    constant_valuest tmp_values = known_values;
    tmp_values.set_to(
      symbol_exprt(ID_cprover_rounding_mode_str, integer_typet()),
      from_integer(rounding_modes[i], integer_typet()));
    exprt result = expr;
    if(replace_constants_and_simplify(tmp_values, result, ns))
    {
      return true;
    }
    else if(i == 0)
    {
      first_result = result;
    }
    else if(result != first_result)
    {
      return true;
    }
  }
  expr = first_result;
  return false;
}

/// Attempt to evaluate expression using domain knowledge
/// This function changes the expression that is passed into it.
/// \param expr: The expression to evaluate
/// \param ns: The namespace for symbols in the expression
/// \return True if the expression is unchanged, false otherwise
bool constant_valuest::partial_evaluate(exprt &expr, const namespacet &ns) const
{
  // if the current rounding mode is top we can
  // still get a non-top result by trying all rounding
  // modes and checking if the results are all the same
  if(!is_constant(ID_cprover_rounding_mode_str))
    return partial_evaluate_with_all_rounding_modes(*this, expr, ns);

  return replace_constants_and_simplify(*this, expr, ns);
}
