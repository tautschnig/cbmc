/*******************************************************************\

Module: Symbolic Execution

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

/// \file
/// Symbolic Execution

#include "goto_symex_state.h"

#include <cstdlib>
#include <iostream>

#include <util/base_exceptions.h>
#include <util/exception_utils.h>
#include <util/expr_util.h>
#include <util/format.h>
#include <util/format_expr.h>
#include <util/invariant.h>
#include <util/prefix.h>
#include <util/std_expr.h>

#include <analyses/dirty.h>

#include "symex_target_equation.h"

static void get_l1_name(exprt &expr);

goto_symex_statet::goto_symex_statet(const symex_targett::sourcet &_source)
  : goto_statet(_source), symex_target(nullptr), record_events(true)
{
  threads.resize(1);
  new_frame();
}

goto_symex_statet::~goto_symex_statet()=default;

/// write to a variable
static bool check_renaming(const exprt &expr);

static bool check_renaming(const typet &type)
{
  if(type.id()==ID_array)
    return check_renaming(to_array_type(type).size());
  else if(type.id() == ID_struct || type.id() == ID_union)
  {
    for(const auto &c : to_struct_union_type(type).components())
      if(check_renaming(c.type()))
        return true;
  }
  else if(type.has_subtype())
    return check_renaming(to_type_with_subtype(type).subtype());

  return false;
}

static bool check_renaming_l1(const exprt &expr)
{
  if(check_renaming(expr.type()))
    return true;

  if(expr.id()==ID_symbol)
  {
    if(!expr.get_bool(ID_C_SSA_symbol))
      return expr.type().id()!=ID_code;
    if(!to_ssa_expr(expr).get_level_2().empty())
      return true;
    if(to_ssa_expr(expr).get_original_expr().type()!=expr.type())
      return true;
  }
  else
  {
    forall_operands(it, expr)
      if(check_renaming_l1(*it))
        return true;
  }

  return false;
}

static bool check_renaming(const exprt &expr)
{
  if(check_renaming(expr.type()))
    return true;

  if(
    expr.id() == ID_address_of &&
    to_address_of_expr(expr).object().id() == ID_symbol)
  {
    return check_renaming_l1(to_address_of_expr(expr).object());
  }
  else if(
    expr.id() == ID_address_of &&
    to_address_of_expr(expr).object().id() == ID_index)
  {
    const auto index_expr = to_index_expr(to_address_of_expr(expr).object());
    return check_renaming_l1(index_expr.array()) ||
           check_renaming(index_expr.index());
  }
  else if(expr.id()==ID_symbol)
  {
    if(!expr.get_bool(ID_C_SSA_symbol))
      return expr.type().id()!=ID_code;
    if(to_ssa_expr(expr).get_level_2().empty())
      return true;
    if(to_ssa_expr(expr).get_original_expr().type()!=expr.type())
      return true;
  }
  else
  {
    forall_operands(it, expr)
      if(check_renaming(*it))
        return true;
  }

  return false;
}

class goto_symex_is_constantt : public is_constantt
{
protected:
  bool is_constant(const exprt &expr) const override
  {
    if(expr.id() == ID_mult)
    {
      // propagate stuff with sizeof in it
      forall_operands(it, expr)
      {
        if(it->find(ID_C_c_sizeof_type).is_not_nil())
          return true;
        else if(!is_constant(*it))
          return false;
      }

      return true;
    }
    else if(expr.id() == ID_with)
    {
      // this is bad
      /*
      forall_operands(it, expr)
      if(!is_constant(expr.op0()))
      return false;

      return true;
      */
      return false;
    }

    return is_constantt::is_constant(expr);
  }
};

void goto_symex_statet::assignment(
  ssa_exprt &lhs, // L0/L1
  const exprt &rhs,  // L2
  const namespacet &ns,
  bool rhs_is_simplified,
  bool record_value,
  bool allow_pointer_unsoundness)
{
  // identifier should be l0 or l1, make sure it's l1
  rename(lhs, ns, L1);
  irep_idt l1_identifier=lhs.get_identifier();

  // the type might need renaming
  rename(lhs.type(), l1_identifier, ns);
  lhs.update_type();
  if(run_validation_checks)
  {
    DATA_INVARIANT(!check_renaming_l1(lhs), "lhs renaming failed on l1");
  }

#if 0
  PRECONDITION(l1_identifier != get_original_name(l1_identifier)
      || l1_identifier == guard_identifier()
      || ns.lookup(l1_identifier).is_shared()
      || has_prefix(id2string(l1_identifier), "symex::invalid_object")
      || has_prefix(id2string(l1_identifier), "symex_dynamic::dynamic_object"));
#endif

  // do the l2 renaming
  const auto level2_it =
    level2.current_names.emplace(l1_identifier, std::make_pair(lhs, 0)).first;
  symex_renaming_levelt::increase_counter(level2_it);
  set_l2_indices(lhs, ns);

  // in case we happen to be multi-threaded, record the memory access
  bool is_shared=l2_thread_write_encoding(lhs, ns);

  if(run_validation_checks)
  {
    DATA_INVARIANT(!check_renaming(lhs), "lhs renaming failed on l2");
    DATA_INVARIANT(!check_renaming(rhs), "rhs renaming failed on l2");
  }

  // see #305 on GitHub for a simple example and possible discussion
  if(is_shared && lhs.type().id() == ID_pointer && !allow_pointer_unsoundness)
    throw unsupported_operation_exceptiont(
      "pointer handling for concurrency is unsound");

  // for value propagation -- the RHS is L2

  if(!is_shared && record_value && goto_symex_is_constantt()(rhs))
    propagation[l1_identifier] = rhs;
  else
    propagation.erase(l1_identifier);

  {
    // update value sets
    exprt l1_rhs(rhs);
    get_l1_name(l1_rhs);

    ssa_exprt l1_lhs(lhs);
    l1_lhs.remove_level_2();

    if(run_validation_checks)
    {
      DATA_INVARIANT(!check_renaming_l1(l1_lhs), "lhs renaming failed on l1");
      DATA_INVARIANT(!check_renaming_l1(l1_rhs), "rhs renaming failed on l1");
    }

    value_set.assign(l1_lhs, l1_rhs, ns, rhs_is_simplified, is_shared);
  }

  #if 0
  std::cout << "Assigning " << l1_identifier << '\n';
  value_set.output(ns, std::cout);
  std::cout << "**********************\n";
  #endif
}

void goto_symex_statet::set_l0_indices(
  ssa_exprt &ssa_expr,
  const namespacet &ns)
{
  level0(ssa_expr, ns, source.thread_nr);
}

void goto_symex_statet::set_l1_indices(
  ssa_exprt &ssa_expr,
  const namespacet &ns)
{
  if(!ssa_expr.get_level_2().empty())
    return;
  if(!ssa_expr.get_level_1().empty())
    return;
  level0(ssa_expr, ns, source.thread_nr);
  level1(ssa_expr);
}

void goto_symex_statet::set_l2_indices(
  ssa_exprt &ssa_expr,
  const namespacet &ns)
{
  if(!ssa_expr.get_level_2().empty())
    return;
  level0(ssa_expr, ns, source.thread_nr);
  level1(ssa_expr);
  ssa_expr.set_level_2(level2.current_count(ssa_expr.get_identifier()));
}

void goto_symex_statet::rename(
  exprt &expr,
  const namespacet &ns,
  levelt level)
{
  if(auto renamed = rename_expr(expr, ns, level))
    expr = std::move(*renamed);
}

optionalt<exprt> goto_symex_statet::rename_expr(
  const exprt &expr,
  const namespacet &ns,
  levelt level)
{
  // rename all the symbols with their last known value

  if(expr.id()==ID_symbol &&
     expr.get_bool(ID_C_SSA_symbol))
  {
    ssa_exprt ssa = to_ssa_expr(expr);

    if(level == L0)
    {
      set_l0_indices(ssa, ns);
    }
    else if(level == L1)
    {
      set_l1_indices(ssa, ns);
    }
    else if(level==L2)
    {
      set_l1_indices(ssa, ns);

      if(l2_thread_read_encoding(ssa, ns))
      {
        // renaming taken care of by l2_thread_encoding
      }
      else if(!ssa.get_level_2().empty())
      {
        // already at L2
      }
      else
      {
        // We also consider propagation if we go up to L2.
        // L1 identifiers are used for propagation!
        auto p_it = propagation.find(ssa.get_identifier());

        if(p_it.second)
          return p_it.first; // already L2
        else
        {
          if(auto renamed_type = rename_type(expr.type(), ssa.get_identifier(), ns, level))
          {
            ssa.type() = std::move(*renamed_type);
            ssa.update_type();
          }

          set_l2_indices(ssa, ns);
        }
      }

      return std::move(ssa);
    }

    if(auto renamed_type = rename_type(expr.type(), ssa.get_identifier(), ns, level))
    {
      ssa.type() = std::move(*renamed_type);
      ssa.update_type();
    }

    return std::move(ssa);
  }
  else if(expr.id()==ID_symbol)
  {
    // we never rename function symbols
    if(expr.type().id() == ID_code)
    {
      if(auto renamed_type = 
      rename_type(
        expr.type(),
        to_symbol_expr(expr).get_identifier(),
        ns,
        level))
      {
        exprt result = expr;
        result.type() = std::move(*renamed_type);
        return std::move(result);
      }
      else
        return {};
    }

    ssa_exprt ssa(expr);
    if(auto renamed = rename_expr(ssa, ns, level))
      return renamed;
    else
      return std::move(ssa);
  }
  else if(expr.id()==ID_address_of)
  {
    auto &address_of_expr = to_address_of_expr(expr);
    auto renamed_object = rename_address(address_of_expr.object(), ns, level);

    if(renamed_object.has_value())
    {
      address_of_exprt result = address_of_expr;
      result.object() = std::move(*renamed_object);
      to_pointer_type(result.type()).subtype() = result.object().type();
      return std::move(result);
    }
    else
      return {};
  }
  else
  {
    exprt result = expr;
    bool modified = false;

    if(auto renamed_type = rename_type(expr.type(), irep_idt(), ns, level))
    {
      result.type() = std::move(*renamed_type);
      modified = true;
    }

    // do this recursively
    exprt::operandst::iterator op_it = result.operands().begin();
    forall_operands(it, expr)
    {
      if(auto renamed_op = rename_expr(*it, ns, level))
      {
        *op_it = std::move(*renamed_op);
        modified = true;
      }
      ++op_it;
    }

    INVARIANT(
      (result.id() != ID_with ||
       result.type() == to_with_expr(result).old().type()) &&
        (result.id() != ID_if ||
         (result.type() == to_if_expr(result).true_case().type() &&
          result.type() == to_if_expr(result).false_case().type())),
      "Type of renamed expr should be the same as operands for with_exprt and "
      "if_exprt");

    if(modified)
      return std::move(result);
    else
      return {};
  }
}

/// thread encoding
bool goto_symex_statet::l2_thread_read_encoding(
  ssa_exprt &expr,
  const namespacet &ns)
{
  // do we have threads?
  if(threads.size()<=1)
    return false;

  // is it a shared object?
  PRECONDITION(dirty != nullptr);
  const irep_idt &obj_identifier=expr.get_object_name();
  if(
    obj_identifier == guard_identifier() ||
    (!ns.lookup(obj_identifier).is_shared() && !(*dirty)(obj_identifier)))
  {
    return false;
  }

  ssa_exprt ssa_l1=expr;
  ssa_l1.remove_level_2();
  const irep_idt &l1_identifier=ssa_l1.get_identifier();
  const exprt guard_as_expr = guard.as_expr();

  // see whether we are within an atomic section
  if(atomic_section_id!=0)
  {
    guardt write_guard{false_exprt{}};

    const auto a_s_writes = written_in_atomic_section.find(ssa_l1);
    if(a_s_writes!=written_in_atomic_section.end())
    {
      for(const auto &guard_in_list : a_s_writes->second)
      {
        guardt g = guard_in_list;
        g-=guard;
        if(g.is_true())
          // there has already been a write to l1_identifier within
          // this atomic section under the same guard, or a guard
          // that implies the current one
          return false;

        write_guard |= guard_in_list;
      }
    }

    not_exprt no_write(write_guard.as_expr());

    // we cannot determine for sure that there has been a write already
    // so generate a read even if l1_identifier has been written on
    // all branches flowing into this read
    guardt read_guard{false_exprt{}};

    a_s_r_entryt &a_s_read=read_in_atomic_section[ssa_l1];
    for(const auto &a_s_read_guard : a_s_read.second)
    {
      guardt g = a_s_read_guard; // copy
      g-=guard;
      if(g.is_true())
        // there has already been a read l1_identifier within
        // this atomic section under the same guard, or a guard
        // that implies the current one
        return false;

      read_guard |= a_s_read_guard;
    }

    guardt cond = read_guard;
    if(!no_write.op().is_false())
      cond |= guardt{no_write.op()};

    if_exprt tmp(cond.as_expr(), ssa_l1, ssa_l1);
    set_l2_indices(to_ssa_expr(tmp.true_case()), ns);

    if(a_s_read.second.empty())
    {
      auto level2_it =
        level2.current_names.emplace(l1_identifier, std::make_pair(ssa_l1, 0))
          .first;
      symex_renaming_levelt::increase_counter(level2_it);
      a_s_read.first=level2.current_count(l1_identifier);
    }

    to_ssa_expr(tmp.false_case()).set_level_2(a_s_read.first);

    if(cond.is_false())
    {
      exprt t=tmp.false_case();
      t.swap(tmp);
    }

    const bool record_events_bak=record_events;
    record_events=false;
    assignment(ssa_l1, tmp, ns, true, true);
    record_events=record_events_bak;

    symex_target->assignment(
      guard_as_expr,
      ssa_l1,
      ssa_l1,
      ssa_l1.get_original_expr(),
      tmp,
      source,
      symex_targett::assignment_typet::PHI);

    set_l2_indices(ssa_l1, ns);
    expr=ssa_l1;

    a_s_read.second.push_back(guard);
    if(!no_write.op().is_false())
      a_s_read.second.back().add(no_write);

    return true;
  }

  const auto level2_it =
    level2.current_names.emplace(l1_identifier, std::make_pair(ssa_l1, 0))
      .first;

  // No event and no fresh index, but avoid constant propagation
  if(!record_events)
  {
    set_l2_indices(ssa_l1, ns);
    expr=ssa_l1;
    return true;
  }

  // produce a fresh L2 name
  symex_renaming_levelt::increase_counter(level2_it);
  set_l2_indices(ssa_l1, ns);
  expr=ssa_l1;

  // and record that
  INVARIANT_STRUCTURED(
    symex_target!=nullptr, nullptr_exceptiont, "symex_target is null");
  symex_target->shared_read(guard_as_expr, expr, atomic_section_id, source);

  return true;
}

/// thread encoding
bool goto_symex_statet::l2_thread_write_encoding(
  const ssa_exprt &expr,
  const namespacet &ns)
{
  if(!record_events)
    return false;

  // is it a shared object?
  PRECONDITION(dirty != nullptr);
  const irep_idt &obj_identifier=expr.get_object_name();
  if(
    obj_identifier == guard_identifier() ||
    (!ns.lookup(obj_identifier).is_shared() && !(*dirty)(obj_identifier)))
  {
    return false; // not shared
  }

  // see whether we are within an atomic section
  if(atomic_section_id!=0)
  {
    ssa_exprt ssa_l1=expr;
    ssa_l1.remove_level_2();

    written_in_atomic_section[ssa_l1].push_back(guard);
    return false;
  }

  // record a shared write
  symex_target->shared_write(
    guard.as_expr(),
    expr,
    atomic_section_id,
    source);

  // do we have threads?
  return threads.size()>1;
}

optionalt<exprt> goto_symex_statet::rename_address(
  const exprt &expr,
  const namespacet &ns,
  levelt level)
{
  if(expr.id()==ID_symbol &&
     expr.get_bool(ID_C_SSA_symbol))
  {
    ssa_exprt ssa = to_ssa_expr(expr);

    // only do L1!
    set_l1_indices(ssa, ns);

    if(auto renamed_type = rename_type(expr.type(), ssa.get_identifier(), ns, level))
    {
      ssa.type() = std::move(*renamed_type);
      ssa.update_type();
    }

    return std::move(ssa);
  }
  else if(expr.id()==ID_symbol)
  {
    ssa_exprt ssa(expr);
    if(auto renamed = rename_address(ssa, ns, level))
      return renamed;
    else
      return std::move(ssa);
  }
  else if(expr.id()==ID_index)
  {
    const index_exprt &index_expr = to_index_expr(expr);

    auto renamed_array = rename_address(index_expr.array(), ns, level);

    // the index is not an address
    auto renamed_index = rename_expr(index_expr.index(), ns, level);

    if(renamed_array.has_value() || renamed_index.has_value())
    {
      index_exprt result = index_expr;

      if(renamed_array.has_value())
      {
        result.array() = std::move(*renamed_array);
        result.type() = to_array_type(result.array().type()).subtype();
      }

      if(renamed_index.has_value())
        result.index() = std::move(*renamed_index);

      return std::move(result);
    }
    else
      return {};
  }
  else if(expr.id()==ID_if)
  {
    // the condition is not an address
    const if_exprt &if_expr = to_if_expr(expr);
    auto renamed_cond = rename_expr(if_expr.cond(), ns, level);
    auto renamed_true = rename_address(if_expr.true_case(), ns, level);
    auto renamed_false = rename_address(if_expr.false_case(), ns, level);

    if(renamed_cond.has_value() || renamed_true.has_value() || renamed_false.has_value())
    {
      if_exprt result = if_expr;

      if(renamed_cond.has_value())
        result.cond() = std::move(*renamed_cond);

      if(renamed_true.has_value())
      {
        result.true_case() = std::move(*renamed_true);
        result.type() = result.true_case().type();
      }

      if(renamed_false.has_value())
        result.false_case() = std::move(*renamed_false);

      return std::move(result);
    }
    else
      return {};
  }
  else if(expr.id()==ID_member)
  {
    const member_exprt &member_expr = to_member_expr(expr);

    auto renamed_struct_op = rename_address(member_expr.struct_op(), ns, level);

    // type might not have been renamed in case of nesting of
    // structs and pointers/arrays
    optionalt<typet> renamed_type;
    if(
      renamed_struct_op.has_value() &&
      renamed_struct_op->type().id() != ID_symbol_type &&
      renamed_struct_op->type().id() != ID_struct_tag &&
      renamed_struct_op->type().id() != ID_union_tag)
    {
      const struct_union_typet &su_type=
        to_struct_union_type(renamed_struct_op->type());
      const struct_union_typet::componentt &comp=
        su_type.get_component(member_expr.get_component_name());
      PRECONDITION(comp.is_not_nil());
      renamed_type = comp.type();
    }
    else
      renamed_type = rename_type(expr.type(), irep_idt(), ns, level);

    if(renamed_struct_op.has_value() || renamed_type.has_value())
    {
      member_exprt result = member_expr;

      if(renamed_struct_op.has_value())
        result.struct_op() = std::move(*renamed_struct_op);

      if(renamed_type.has_value())
        result.type() = std::move(*renamed_type);

      return std::move(result);
    }
    else
      return {};
  }
  else
  {
    exprt result = expr;
    bool modified = false;

    // this could go wrong, but we would have to re-typecheck ...
    if(auto renamed_type = rename_type(expr.type(), irep_idt(), ns, level))
    {
      result.type() = std::move(*renamed_type);
      modified = true;
    }

    // do this recursively; we assume here
    // that all the operands are addresses
    exprt::operandst::iterator op_it = result.operands().begin();
    forall_operands(it, expr)
    {
      if(auto renamed_op = rename_address(*it, ns, level))
      {
        *op_it = std::move(*renamed_op);
        modified = true;
      }
      ++op_it;
    }

    if(modified)
      return std::move(result);
    else
      return {};
  }
}

/// Return true if, and only if, the \p type or one of its subtypes requires SSA
/// renaming. Renaming is necessary when symbol expressions occur within the
/// type, which is the case for arrays of non-constant size.
static bool requires_renaming(const typet &type, const namespacet &ns)
{
  if(type.id() == ID_array)
  {
    const auto &array_type = to_array_type(type);
    return requires_renaming(array_type.subtype(), ns) ||
           !array_type.size().is_constant();
  }
  else if(
    type.id() == ID_struct || type.id() == ID_union || type.id() == ID_class)
  {
    const struct_union_typet &s_u_type = to_struct_union_type(type);
    const struct_union_typet::componentst &components = s_u_type.components();

    for(auto &component : components)
    {
      // be careful, or it might get cyclic
      if(
        component.type().id() != ID_pointer &&
        requires_renaming(component.type(), ns))
      {
        return true;
      }
    }

    return false;
  }
  else if(type.id() == ID_pointer)
  {
    return requires_renaming(to_pointer_type(type).subtype(), ns);
  }
  else if(type.id() == ID_symbol_type)
  {
    const symbolt &symbol = ns.lookup(to_symbol_type(type));
    return requires_renaming(symbol.type, ns);
  }
  else if(type.id() == ID_union_tag)
  {
    const symbolt &symbol = ns.lookup(to_union_tag_type(type));
    return requires_renaming(symbol.type, ns);
  }
  else if(type.id() == ID_struct_tag)
  {
    const symbolt &symbol = ns.lookup(to_struct_tag_type(type));
    return requires_renaming(symbol.type, ns);
  }

  return false;
}

void goto_symex_statet::rename(
  typet &type,
  const irep_idt &l1_identifier,
  const namespacet &ns,
  levelt level)
{
  if(auto renamed = rename_type(type, l1_identifier, ns, level))
    type = std::move(*renamed);
}

optionalt<typet> goto_symex_statet::rename_type(
  const typet &type,
  const irep_idt &l1_identifier,
  const namespacet &ns,
  levelt level)
{
  // check whether there are symbol expressions in the type; if not, there
  // is no need to expand the struct/union tags in the type
  if(!requires_renaming(type, ns))
    return {};

  // rename all the symbols with their last known value
  // to the given level

  std::pair<l1_typest::iterator, bool> l1_type_entry;
  if(level==L2 &&
     !l1_identifier.empty())
  {
    l1_type_entry=l1_types.insert(std::make_pair(l1_identifier, type));

    if(!l1_type_entry.second) // was already in map
    {
      // do not change a complete array type to an incomplete one

      const typet &type_prev=l1_type_entry.first->second;

      if(type.id()!=ID_array ||
         type_prev.id()!=ID_array ||
         to_array_type(type).is_incomplete() ||
         to_array_type(type_prev).is_complete())
      {
        return l1_type_entry.first->second;
      }
    }
  }

  optionalt<typet> result;

  if(type.id()==ID_array)
  {
    auto &array_type = to_array_type(type);
    auto renamed_subtype = rename_type(array_type.subtype(), irep_idt(), ns, level);
    auto renamed_size = rename_expr(array_type.size(), ns, level);

    if(renamed_subtype.has_value() || renamed_size.has_value())
    {
      array_typet result_type = array_type;

      if(renamed_subtype.has_value())
        result_type.subtype() = std::move(*renamed_subtype);

      if(renamed_size.has_value())
        result_type.size() = std::move(*renamed_size);

      result = std::move(result_type);
    }
  }
  else if(type.id() == ID_struct || type.id() == ID_union)
  {
    struct_union_typet s_u_type = to_struct_union_type(type);
    bool modified = false;

    struct_union_typet::componentst::iterator comp_it = s_u_type.components().begin();
    for(auto &component : to_struct_union_type(type).components())
    {
      // be careful, or it might get cyclic
      if(component.type().id() == ID_array)
      {
        if(auto renamed_expr = rename_expr(to_array_type(component.type()).size(), ns, level))
        {
          to_array_type(comp_it->type()).size() = std::move(*renamed_expr);
          modified = true;
        }
      }
      else if(component.type().id() != ID_pointer)
      {
        if(auto renamed_type = rename_type(component.type(), irep_idt(), ns, level))
        {
          comp_it->type() = std::move(*renamed_type);
          modified = true;
        }
      }

      ++comp_it;
    }

    if(modified)
      result = std::move(s_u_type);
  }
  else if(type.id()==ID_pointer)
  {
    if(auto renamed_subtype = rename_type(to_pointer_type(type).subtype(), irep_idt(), ns, level))
    {
      pointer_typet pointer_type = to_pointer_type(type);
      pointer_type.subtype() = std::move(*renamed_subtype);
      result = std::move(pointer_type);
    }
  }
  else if(type.id() == ID_symbol_type)
  {
    const symbolt &symbol = ns.lookup(to_symbol_type(type));
    result = rename_type(symbol.type, l1_identifier, ns, level);
    if(!result.has_value())
      result = symbol.type;
  }
  else if(type.id() == ID_union_tag)
  {
    const symbolt &symbol = ns.lookup(to_union_tag_type(type));
    result = rename_type(symbol.type, l1_identifier, ns, level);
    if(!result.has_value())
      result = symbol.type;
  }
  else if(type.id() == ID_struct_tag)
  {
    const symbolt &symbol = ns.lookup(to_struct_tag_type(type));
    result = rename_type(symbol.type, l1_identifier, ns, level);
    if(!result.has_value())
      result = symbol.type;
  }

  if(level==L2 && !l1_identifier.empty() && result.has_value())
    l1_type_entry.first->second = *result;

  return result;
}

static void get_l1_name(exprt &expr)
{
  // do not reset the type !

  if(expr.id()==ID_symbol &&
     expr.get_bool(ID_C_SSA_symbol))
    to_ssa_expr(expr).remove_level_2();
  else
    Forall_operands(it, expr)
      get_l1_name(*it);
}

/// Dumps the current state of symex, printing the function name and location
/// number for each stack frame in the currently active thread.
/// This is for use from the debugger or in debug code; please don't delete it
/// just because it isn't called at present.
/// \param out: stream to write to
void goto_symex_statet::print_backtrace(std::ostream &out) const
{
  if(threads[source.thread_nr].call_stack.empty())
  {
    out << "No stack!\n";
    return;
  }

  out << source.function_id << " " << source.pc->location_number << "\n";

  for(auto stackit = threads[source.thread_nr].call_stack.rbegin(),
           stackend = threads[source.thread_nr].call_stack.rend();
      stackit != stackend;
      ++stackit)
  {
    const auto &frame = *stackit;
    out << frame.calling_location.function_id << " "
        << frame.calling_location.pc->location_number << "\n";
  }
}

/// Print the constant propagation map in a human-friendly format.
/// This is primarily for use from the debugger; please don't delete me just
/// because there aren't any current callers.
void goto_statet::output_propagation_map(std::ostream &out)
{
  sharing_mapt<irep_idt, exprt>::viewt view;
  propagation.get_view(view);

  for(const auto &name_value : view)
  {
    out << name_value.first << " <- " << format(name_value.second) << "\n";
  }
}
