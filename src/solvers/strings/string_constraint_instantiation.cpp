/*******************************************************************\

Module: Defines functions related to string constraints.

Author: Jesse Sigal, jesse.sigal@diffblue.com

\*******************************************************************/

/// \file
/// Defines related function for string constraints.

#include "string_constraint_instantiation.h"

#include <util/arith_tools.h>
#include <util/expr_iterator.h>
#include <util/format_expr.h>

#include "string_constraint.h"

#include <algorithm>
#include <functional>
#include <list>
#include <unordered_set>

/// Look for symbol \p qvar in the expression \p index and return true if found
/// \return True, iff \p qvar appears in \p index.
static bool contains(const exprt &index, const symbol_exprt &qvar)
{
  return std::find(index.depth_begin(), index.depth_end(), qvar) !=
         index.depth_end();
}

/// Find indexes of `str` used in `expr` that contains `qvar`, for instance
/// with arguments ``(str[k+1]=='a')``, `str`, and `k`, the function should
/// return `k+1`.
/// \param [in] expr: the expression to search
/// \param [in] str: the string which must be indexed
/// \param [in] qvar: the universal variable that must be in the index
/// \return index expressions in `expr` on `str` containing `qvar`.
static std::unordered_set<exprt, irep_hash>
find_indexes(const exprt &expr, const exprt &str, const symbol_exprt &qvar)
{
  // Each node of the DAGs once: the arrays of the accesses are if-expressions
  // over strings merged at join points of a program, which share their
  // subexpressions, and whose trees can be very large.
  std::unordered_map<const void *, bool> has_str;
  std::function<bool(const exprt &)> mentions_str = [&](const exprt &e)
  {
    const void *const node = &e.read();
    const auto found = has_str.find(node);
    if(found != has_str.end())
      return found->second;
    bool result = e == str;
    for(const auto &op : e.operands())
    {
      if(result)
        break;
      result = mentions_str(op);
    }
    has_str.emplace(node, result);
    return result;
  };
  std::unordered_set<exprt, irep_hash> result;
  std::unordered_set<const void *> visited;
  std::vector<std::reference_wrapper<const exprt>> stack({expr});
  while(!stack.empty())
  {
    const exprt &e = stack.back();
    stack.pop_back();
    if(!visited.insert(&e.read()).second)
      continue;
    if(const auto index_expr = expr_try_dynamic_cast<index_exprt>(e))
    {
      if(
        mentions_str(index_expr->array()) &&
        contains(index_expr->index(), qvar))
      {
        result.insert(index_expr->index());
      }
    }
    for(const auto &op : e.operands())
      stack.emplace_back(op);
  }
  return result;
}

linear_functiont::linear_functiont(const exprt &f)
{
  type = f.type();
  // list of expressions to process with a boolean flag telling whether they
  // appear positively or negatively (true is for positive)
  std::list<std::pair<exprt, bool>> to_process;
  to_process.emplace_back(f, true);

  while(!to_process.empty())
  {
    const exprt cur = to_process.back().first;
    bool positive = to_process.back().second;
    to_process.pop_back();
    if(auto integer = numeric_cast<mp_integer>(cur))
    {
      constant_coefficient += positive ? integer.value() : -integer.value();
    }
    else if(cur.id() == ID_plus)
    {
      for(const auto &op : cur.operands())
        to_process.emplace_back(op, positive);
    }
    else if(cur.id() == ID_minus)
    {
      to_process.emplace_back(to_minus_expr(cur).op1(), !positive);
      to_process.emplace_back(to_minus_expr(cur).op0(), positive);
    }
    else if(cur.id() == ID_unary_minus)
    {
      to_process.emplace_back(to_unary_minus_expr(cur).op(), !positive);
    }
    else
    {
      if(positive)
        ++coefficients[cur];
      else
        --coefficients[cur];
    }
  }
}

void linear_functiont::add(const linear_functiont &other)
{
  PRECONDITION(type == other.type);
  constant_coefficient += other.constant_coefficient;
  for(auto pair : other.coefficients)
    coefficients[pair.first] += pair.second;
}

exprt linear_functiont::to_expr(bool negated) const
{
  exprt sum = nil_exprt{};
  const exprt constant_expr = from_integer(constant_coefficient, type);
  if(constant_coefficient != 0)
    sum = negated ? (exprt)unary_minus_exprt{constant_expr} : constant_expr;

  for(const auto &term : coefficients)
  {
    const exprt &t = term.first;
    const mp_integer factor = negated ? (-term.second) : term.second;
    if(factor == -1)
    {
      if(sum.is_nil())
        sum = unary_minus_exprt(t);
      else
        sum = minus_exprt(sum, t);
    }
    else if(factor == 1)
    {
      if(sum.is_nil())
        sum = t;
      else
        sum = plus_exprt(sum, t);
    }
    else if(factor != 0)
    {
      const mult_exprt to_add{t, from_integer(factor, t.type())};
      if(sum.is_nil())
        sum = to_add;
      else
        sum = plus_exprt(sum, to_add);
    }
  }
  return sum.is_nil() ? from_integer(0, type) : sum;
}

exprt linear_functiont::solve(
  linear_functiont f,
  const exprt &var,
  const exprt &val)
{
  auto it = f.coefficients.find(var);
  PRECONDITION(it != f.coefficients.end());
  PRECONDITION(it->second == 1 || it->second == -1);
  const bool positive = it->second == 1;

  // Transform `f` into `f(var <- 0)`
  f.coefficients.erase(it);
  // Transform `f(var <- 0)` into `f(var <- 0) - val`
  f.add(linear_functiont{unary_minus_exprt{val}});

  // If the coefficient of var is 1 then solution `val - f(var <- 0),
  // otherwise `f(var <- 0) - val`
  return f.to_expr(positive);
}

std::string linear_functiont::format()
{
  std::ostringstream stream;
  stream << constant_coefficient;
  for(const auto &pair : coefficients)
    stream << " + " << pair.second << " * " << ::format(pair.first);
  return stream.str();
}

/// The instance of \p axiom for \p val, by its \p indexes on a string.
static exprt instantiate(
  const string_constraintt &axiom,
  const std::unordered_set<exprt, irep_hash> &indexes,
  const exprt &val)
{
  exprt::operandst conjuncts;
  for(const auto &index : indexes)
  {
    const exprt univ_var_value =
      linear_functiont::solve(linear_functiont{index}, axiom.univ_var, val);
    implies_exprt instance(
      and_exprt(
        binary_relation_exprt(axiom.univ_var, ID_ge, axiom.lower_bound),
        binary_relation_exprt(axiom.univ_var, ID_lt, axiom.upper_bound)),
      axiom.body);
    replace_expr_shared(axiom.univ_var, univ_var_value, instance);
    conjuncts.push_back(instance);
  }
  return conjunction(conjuncts);
}

exprt instantiate(
  const string_constraintt &axiom,
  const exprt &str,
  const exprt &val)
{
  return instantiate(axiom, find_indexes(axiom.body, str, axiom.univ_var), val);
}

axiom_indexest::axiom_indexest(const string_constraintt &axiom)
{
  // The subexpressions of each array, once for each node of the DAG.
  std::unordered_map<const void *, std::vector<exprt>> subexpressions;
  const auto subexpressions_of =
    [&](const exprt &array) -> const std::vector<exprt> &
  {
    auto inserted = subexpressions.emplace(&array.read(), std::vector<exprt>{});
    if(!inserted.second)
      return inserted.first->second;
    std::vector<exprt> &result = inserted.first->second;
    std::unordered_set<const void *> visited;
    std::vector<std::reference_wrapper<const exprt>> stack({array});
    while(!stack.empty())
    {
      const exprt &e = stack.back();
      stack.pop_back();
      if(!visited.insert(&e.read()).second)
        continue;
      result.push_back(e);
      for(const auto &op : e.operands())
        stack.emplace_back(op);
    }
    return result;
  };
  std::unordered_set<const void *> visited;
  std::vector<std::reference_wrapper<const exprt>> stack({axiom.body});
  while(!stack.empty())
  {
    const exprt &e = stack.back();
    stack.pop_back();
    if(!visited.insert(&e.read()).second)
      continue;
    if(const auto index_expr = expr_try_dynamic_cast<index_exprt>(e))
    {
      if(contains(index_expr->index(), axiom.univ_var))
      {
        for(const auto &sub : subexpressions_of(index_expr->array()))
          by_string[sub].insert(index_expr->index());
      }
    }
    for(const auto &op : e.operands())
      stack.emplace_back(op);
  }
}

const std::unordered_set<exprt, irep_hash> &
axiom_indexest::of(const exprt &str) const
{
  static const std::unordered_set<exprt, irep_hash> none;
  const auto found = by_string.find(str);
  return found == by_string.end() ? none : found->second;
}

std::vector<exprt> instantiate(
  const string_constraintt &axiom,
  const axiom_indexest &indexes,
  const exprt &str,
  const std::set<exprt> &vals)
{
  const auto &of_str = indexes.of(str);
  std::vector<exprt> result;
  for(const auto &val : vals)
    result.push_back(instantiate(axiom, of_str, val));
  return result;
}

/// Instantiates a quantified formula representing `not_contains` by
/// substituting the quantifiers and generating axioms.
/// \related string_refinementt
/// \param [in] axiom: the axiom to instantiate
/// \param [in] index_pairs: pair of indexes for `axiom.s0()`and `axiom.s1()`
/// \param [in] witnesses: `axiom`'s witnesses for non containment
/// \return the lemmas produced through instantiation: for each value of the
///   universal variable, that the witness is in the existential range, and
///   for each index pair, the instance of the axiom
std::vector<exprt> instantiate_not_contains(
  const string_not_contains_constraintt &axiom,
  const std::set<std::pair<exprt, exprt>> &index_pairs,
  const std::unordered_map<string_not_contains_constraintt, symbol_exprt>
    &witnesses)
{
  std::vector<exprt> lemmas;

  const array_string_exprt s0 = axiom.s0;
  const array_string_exprt s1 = axiom.s1;

  // The witness f(x) is a Skolem function for the existential quantifier,
  // so it is in the existential bounds for each x the premise holds for.
  // Without that, f(x) can be out of bounds, which satisfies the instances
  // below for any index pairs, and only counter-examples would refine.
  std::set<exprt> values;
  for(const auto &pair : index_pairs)
  {
    const minus_exprt val(pair.first, pair.second);
    if(!values.insert(val).second)
      continue;
    const and_exprt universal_bound(
      binary_relation_exprt(axiom.univ_lower_bound, ID_le, val),
      binary_relation_exprt(axiom.univ_upper_bound, ID_gt, val));
    const exprt f = index_exprt(witnesses.at(axiom), val);
    lemmas.push_back(implies_exprt{
      and_exprt{axiom.premise, universal_bound},
      and_exprt{
        binary_relation_exprt{axiom.exists_lower_bound, ID_le, f},
        binary_relation_exprt{axiom.exists_upper_bound, ID_gt, f}}});
  }

  for(const auto &pair : index_pairs)
  {
    // We have s0[x+f(x)] and s1[f(x)], so to have i0 indexing s0 and i1
    // indexing s1, we need x = i0 - i1 and f(i0 - i1) = f(x) = i1.
    const exprt &i0 = pair.first;
    const exprt &i1 = pair.second;
    const minus_exprt val(i0, i1);
    const and_exprt universal_bound(
      binary_relation_exprt(axiom.univ_lower_bound, ID_le, val),
      binary_relation_exprt(axiom.univ_upper_bound, ID_gt, val));
    const exprt f = index_exprt(witnesses.at(axiom), val);
    const equal_exprt relevancy(f, i1);
    const and_exprt premise(relevancy, axiom.premise, universal_bound);

    const notequal_exprt differ(s0[i0], s1[i1]);
    const and_exprt existential_bound(
      binary_relation_exprt(axiom.exists_lower_bound, ID_le, i1),
      binary_relation_exprt(axiom.exists_upper_bound, ID_gt, i1));
    const and_exprt body(differ, existential_bound);

    const implies_exprt lemma(premise, body);
    lemmas.push_back(lemma);
  }

  return lemmas;
}
