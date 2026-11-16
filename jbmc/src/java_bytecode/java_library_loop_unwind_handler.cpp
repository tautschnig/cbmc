/*******************************************************************\

Module: Unwind the counted loops of the Java library models

Author: Diffblue Ltd.

\*******************************************************************/

/// \file
/// Unwind the counted loops of the Java library models

#include "java_library_loop_unwind_handler.h"

#include <util/arith_tools.h>
#include <util/prefix.h>
#include <util/ssa_expr.h> // IWYU pragma: keep
#include <util/std_expr.h>

#include <goto-programs/abstract_goto_model.h>

#include <goto-symex/call_stack.h>

/// \p e without its typecasts.
static const exprt &skip_typecasts(const exprt &e)
{
  const exprt *p = &e;
  while(p->id() == ID_typecast)
    p = &to_typecast_expr(*p).op();
  return *p;
}

/// The integer of the constant \p e (under typecasts), if it is one.
static std::optional<mp_integer> constant_value(const exprt &e)
{
  const exprt &c = skip_typecasts(e);
  if(c.id() != ID_constant)
    return {};
  return numeric_cast<mp_integer>(c);
}

/// A relation of a variable and a constant: the variable on the left.
struct counter_relationt
{
  irep_idt variable;
  irep_idt relation;
  mp_integer constant;

  bool holds(const mp_integer &v) const
  {
    if(relation == ID_lt)
      return v < constant;
    if(relation == ID_le)
      return v <= constant;
    if(relation == ID_gt)
      return v > constant;
    if(relation == ID_ge)
      return v >= constant;
    if(relation == ID_equal)
      return v == constant;
    return v != constant;
  }
};

/// The relation of a local variable and a constant that \p guard is (under
/// negations, whose number does not matter for when it changes).
static std::optional<counter_relationt> counter_relation(const exprt &guard)
{
  const exprt *g = &guard;
  while(g->id() == ID_not)
    g = &to_not_expr(*g).op();
  if(
    g->id() != ID_lt && g->id() != ID_le && g->id() != ID_gt &&
    g->id() != ID_ge && g->id() != ID_equal && g->id() != ID_notequal)
  {
    return {};
  }
  const auto &rel = to_binary_relation_expr(*g);
  const exprt &lhs = skip_typecasts(rel.lhs());
  const exprt &rhs = skip_typecasts(rel.rhs());
  irep_idt relation = g->id();
  const exprt *variable = &lhs;
  auto constant = constant_value(rhs);
  if(!constant.has_value())
  {
    // the constant on the left: swap the sides
    constant = constant_value(lhs);
    variable = &rhs;
    if(relation == ID_lt)
      relation = ID_gt;
    else if(relation == ID_gt)
      relation = ID_lt;
    else if(relation == ID_le)
      relation = ID_ge;
    else if(relation == ID_ge)
      relation = ID_le;
  }
  if(!constant.has_value() || variable->id() != ID_symbol)
    return {};
  return counter_relationt{
    to_symbol_expr(*variable).get_identifier(), relation, *constant};
}

/// The constant that \p rhs adds to \p variable (v + c or v - c), if it is
/// such an expression.
static std::optional<mp_integer>
counter_step(const exprt &rhs, const irep_idt &variable)
{
  const exprt &e = skip_typecasts(rhs);
  if(e.id() != ID_plus && e.id() != ID_minus)
    return {};
  const auto &binary = to_binary_expr(e);
  const exprt &lhs = skip_typecasts(binary.op0());
  if(lhs.id() != ID_symbol || to_symbol_expr(lhs).get_identifier() != variable)
  {
    return {};
  }
  const auto c = constant_value(binary.op1());
  if(!c.has_value() || *c == 0)
    return {};
  return e.id() == ID_plus ? *c : -*c;
}

/// Whether \p function is a method of the library (whose models are in
/// these packages).
static bool is_library_function(const irep_idt &function)
{
  const std::string &f = id2string(function);
  return has_prefix(f, "java::java.") || has_prefix(f, "java::javax.") ||
         has_prefix(f, "java::sun.") || has_prefix(f, "java::jdk.") ||
         has_prefix(f, "java::org.cprover.");
}

std::optional<mp_integer> java_library_loop_unwind_handlert::iterations(
  const irep_idt &function,
  unsigned loop_number)
{
  if(!goto_model.can_produce_function(function))
    return {};
  const goto_programt &body = goto_model.get_goto_function(function).body;
  // the backward jump of the loop, and its head
  auto back = body.instructions.end();
  for(auto it = body.instructions.begin(); it != body.instructions.end(); ++it)
  {
    if(it->is_backwards_goto() && it->loop_number == loop_number)
      back = it;
  }
  if(back == body.instructions.end())
    return {};
  const auto head = back->get_target();
  // the condition of the head, on the counter
  if(!head->is_goto() || head->condition().is_true())
    return {};
  const auto relation = counter_relation(head->condition());
  if(!relation.has_value())
    return {};
  const irep_idt &v = relation->variable;
  const auto assigns_v = [&](const goto_programt::instructiont &i)
  {
    if(i.is_assign())
    {
      const exprt &lhs = i.assign_lhs();
      return lhs.id() == ID_symbol && to_symbol_expr(lhs).get_identifier() == v;
    }
    if(i.is_decl())
      return i.decl_symbol().get_identifier() == v;
    if(i.is_function_call())
    {
      const exprt &lhs = i.call_lhs();
      return lhs.id() == ID_symbol && to_symbol_expr(lhs).get_identifier() == v;
    }
    return false;
  };
  // its only assignment in the loop adds a constant
  std::optional<mp_integer> step;
  for(auto it = head;; ++it)
  {
    if(assigns_v(*it))
    {
      if(step.has_value() || !it->is_assign())
        return {};
      step = counter_step(it->assign_rhs(), v);
      if(!step.has_value())
        return {};
    }
    if(it == back)
      break;
  }
  if(!step.has_value())
    return {};
  // a constant just before the loop (past declarations and assignments of
  // other variables)
  std::optional<mp_integer> start;
  for(auto it = head; it != body.instructions.begin();)
  {
    --it;
    if(assigns_v(*it))
    {
      if(it->is_assign())
        start = constant_value(it->assign_rhs());
      break;
    }
    if(
      !it->is_assign() && !it->is_decl() && !it->is_dead() && !it->is_skip() &&
      !it->is_location())
    {
      break;
    }
    if(it->is_target())
      break;
  }
  if(!start.has_value())
    return {};
  // the iterations until the condition changes
  const bool initially = relation->holds(*start);
  mp_integer value = *start;
  for(std::size_t n = 0; n <= java_library_loop_max_iterations; ++n)
  {
    if(relation->holds(value) != initially)
      return mp_integer{n};
    value += *step;
  }
  return {};
}

tvt java_library_loop_unwind_handlert::operator()(
  const call_stackt &context,
  unsigned loop_number,
  unsigned unwind_count,
  unsigned &unwind_max)
{
  if(context.empty())
    return tvt::unknown();
  const irep_idt &function = context.back().function_identifier;
  if(!is_library_function(function))
    return tvt::unknown();
  const auto key = std::make_pair(function, loop_number);
  auto found = cache.find(key);
  if(found == cache.end())
    found = cache.emplace(key, iterations(function, loop_number)).first;
  if(!found->second.has_value())
    return tvt::unknown();
  const auto count = numeric_cast_v<unsigned>(*found->second);
  if(unwind_count <= count)
  {
    unwind_max = count + 1;
    return tvt(false); // unwind it
  }
  return tvt::unknown();
}
