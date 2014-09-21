/*******************************************************************\

Module: Symbolic Execution

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#include <cassert>

#include <util/expr_util.h>
#include <util/rename.h>
#include <util/std_expr.h>
#include <util/find_symbols.h>

#include <pointer-analysis/add_failed_symbols.h>
#include <analyses/dirty.h>

#include "goto_symex.h"

/*******************************************************************\

Function: goto_symext::symex_dead

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_symext::symex_dead(statet &state)
{
  const goto_programt::instructiont &instruction=*state.source.pc;

  const codet &code=to_code(instruction.code);

  if(code.operands().size()!=1)
    throw "dead expects one operand";
  
  if(code.op0().id()!=ID_symbol)
    throw "dead expects symbol as first operand";

  // We increase the L2 renaming to make these non-deterministic.
  // We also prevent propagation of old values.
  
  ssa_exprt ssa(to_symbol_expr(code.op0()));
  state.rename(ssa, ns, goto_symex_statet::L1);

  // in case of pointers, put something into the value set
  if(ns.follow(code.op0().type()).id()==ID_pointer)
  {
    exprt failed=
      get_failed_symbol(to_symbol_expr(code.op0()), ns);
    
    exprt rhs;
    
    if(failed.is_not_nil())
    {
      address_of_exprt address_of_expr;
      address_of_expr.object()=failed;
      address_of_expr.type()=code.op0().type();
      rhs=address_of_expr;
    }
    else
      rhs=exprt(ID_invalid);
    
    state.rename(rhs, ns, goto_symex_statet::L1);
    state.value_set.assign(ssa, rhs, ns, true, false);
  }

<<<<<<< HEAD
  exprt l2_fields;
  state.field_sensitivity.get_fields(ns, ssa, l2_fields);
  std::set<exprt> l2_fields_set;
  find_symbols(l2_fields, l2_fields_set);
=======
  object_zoo.record_dead(ssa, state.source, state.guard);

  ssa_exprt ssa_lhs=to_ssa_expr(ssa);
  const irep_idt &l1_identifier=ssa_lhs.get_identifier();
>>>>>>> 6374633... More object zoo

  for(std::set<exprt>::const_iterator it=l2_fields_set.begin();
      it!=l2_fields_set.end();
      ++it)
  {
    ssa_exprt ssa_lhs=to_ssa_expr(*it);
    const irep_idt &l1_identifier=ssa_lhs.get_identifier();

    // prevent propagation
    state.propagation.remove(l1_identifier);

    // L2 renaming
    if(state.level2.current_names.find(l1_identifier)!=
       state.level2.current_names.end())
      state.level2.increase_counter(l1_identifier);
    const bool record_events=state.record_events;
    state.record_events=false;
    state.rename(ssa_lhs, ns);
    state.record_events=record_events;

    assert(state.dirty);
    if((*state.dirty)(ssa_lhs.get_object_name()) &&
       state.atomic_section_id==0)
      target.shared_write(
        state.guard.as_expr(),
        ssa_lhs,
        state.atomic_section_id,
        state.source);
  }
}
