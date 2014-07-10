/*******************************************************************\

Module: Field-sensitive SSA

Author: Michael Tautschnig, mt@eecs.qmul.ac.uk

\*******************************************************************/

#include <util/simplify_expr.h>
#include <util/std_expr.h>

#include "symex_target.h"
#include "goto_symex_state.h"

#include "field_sensitivity.h"

/*******************************************************************\

Function: field_sensitivityt::operator()

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void field_sensitivityt::operator()(
  const namespacet &ns,
  exprt &expr,
  bool write) const
{
#if 1
  if(expr.id()!=ID_address_of)
    Forall_operands(it, expr)
      operator()(ns, *it, write);

  if(expr.id()==ID_symbol &&
     expr.get_bool(ID_C_SSA_symbol) &&
     !write)
  {
    ssa_exprt ssa=to_ssa_expr(expr);
    get_fields(ns, ssa, expr);
  }
  else if(!write &&
          expr.id()==ID_member &&
          to_member_expr(expr).struct_op().id()==ID_struct)
  {
    simplify(expr, ns);
  }
  else if(write && expr.id()==ID_member)
  {
    member_exprt &member=to_member_expr(expr);

    if(member.struct_op().id()==ID_symbol &&
       member.struct_op().get_bool(ID_C_SSA_symbol) &&
       ns.follow(member.struct_op().type()).id()==ID_struct)
    {
      ssa_exprt tmp=to_ssa_expr(member.struct_op());

      member.struct_op()=tmp.get_original_expr();

      tmp.type()=member.type();
      tmp.set(ID_expression, member);
      tmp.update_identifier();

      expr.swap(tmp);
    }
  }
#endif
}

/*******************************************************************\

Function: field_sensitivityt::get_fields

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void field_sensitivityt::get_fields(
  const namespacet &ns,
  const ssa_exprt &ssa_expr,
  exprt &dest) const
{
#if 1
  if(ns.follow(ssa_expr.type()).id()==ID_struct)
  {
    const exprt &struct_op=ssa_expr.get_original_expr();

    const struct_typet &type=
      to_struct_type(ns.follow(ssa_expr.type()));

    const struct_union_typet::componentst &components=
      type.components();

    dest=struct_exprt(ssa_expr.type());
    dest.reserve_operands(components.size());

    for(struct_union_typet::componentst::const_iterator
        it=components.begin();
        it!=components.end();
        ++it)
    {
      member_exprt member(struct_op, it->get_name(), it->type());

      ssa_exprt tmp=ssa_expr;

      tmp.type()=member.type();
      tmp.set(ID_expression, member);
      tmp.update_identifier();

      exprt tmp_dest;
      get_fields(ns, tmp, tmp_dest);
      dest.copy_to_operands(tmp_dest);
    }
  }
  else
#endif
    dest=ssa_expr;
}

/*******************************************************************\

Function: field_sensitivityt::field_assignments

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void field_sensitivityt::field_assignments(
  const namespacet &ns,
  goto_symex_statet &state,
  symex_targett &target,
  const exprt &lhs) const
{
  exprt lhs_fs=lhs;
  operator()(ns, lhs_fs, false);

  field_assignments_rec(ns, state, target, lhs_fs, lhs);
}

/*******************************************************************\

Function: field_sensitivityt::field_assignments_rec

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void field_sensitivityt::field_assignments_rec(
  const namespacet &ns,
  goto_symex_statet &state,
  symex_targett &target,
  const exprt &lhs_fs,
  const exprt &lhs) const
{
  if(lhs==lhs_fs)
    return;
  else if(lhs_fs.id()==ID_symbol &&
          lhs_fs.get_bool(ID_C_SSA_symbol))
  {
    exprt ssa_rhs=lhs;

    state.rename(ssa_rhs, ns);
    simplify(ssa_rhs, ns);

    ssa_exprt ssa_lhs=to_ssa_expr(lhs_fs);
    state.assignment(ssa_lhs, ssa_rhs, ns, true);

    // do the assignment
    target.assignment(
      state.guard.as_expr(),
      ssa_lhs,
      ssa_lhs, ssa_lhs.get_original_expr(),
      ssa_rhs,
      state.source,
      symex_targett::STATE);
  }
  else if(ns.follow(lhs.type()).id()==ID_struct)
  {
    const struct_typet &type=
      to_struct_type(ns.follow(lhs.type()));

    const struct_union_typet::componentst &components=
      type.components();

    assert(components.empty() ||
           (lhs_fs.has_operands() &&
            lhs_fs.operands().size()==components.size()));

    unsigned number=0;
    for(struct_union_typet::componentst::const_iterator
        it=components.begin();
        it!=components.end();
        ++it, ++number)
    {
      member_exprt member_rhs(lhs, it->get_name(), it->type());
      exprt member_lhs=lhs_fs.operands()[number];

      field_assignments_rec(ns, state, target, member_lhs, member_rhs);
    }
  }
  else if(lhs_fs.has_operands())
  {
    assert(lhs.has_operands() &&
           lhs_fs.operands().size()==lhs.operands().size());

    exprt::operandst::const_iterator fs_it=lhs_fs.operands().begin();
    for(exprt::operandst::const_iterator it=lhs.operands().begin();
        it!=lhs.operands().end();
        ++it, ++fs_it)
      field_assignments_rec(ns, state, target, *fs_it, *it);
  }
  else
  {
    assert(false);
  }
}

/*******************************************************************\

Function: field_sensitivityt::is_indivisible

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool field_sensitivityt::is_indivisible(
  const namespacet &ns,
  const ssa_exprt &expr) const
{
  exprt tmp;
  get_fields(ns, expr, tmp);

  return expr==tmp;
}
