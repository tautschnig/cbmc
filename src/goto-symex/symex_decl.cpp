/*******************************************************************\

Module: Symbolic Execution

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#include <cassert>

#include <util/expr_util.h>
#include <util/rename.h>
#include <util/std_expr.h>
#include <util/message.h>
#include <util/find_symbols.h>

#include <linking/zero_initializer.h>
#include <analyses/dirty.h>

#include "goto_symex.h"

/*******************************************************************\

Function: goto_symext::symex_decl

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_symext::symex_decl(statet &state)
{
  const goto_programt::instructiont &instruction=*state.source.pc;

  const codet &code=to_code(instruction.code);

  if(code.operands().size()==2)
    throw "two-operand decl not supported here";

  if(code.operands().size()!=1)
    throw "decl expects one operand";
  
  if(code.op0().id()!=ID_symbol)
    throw "decl expects symbol as first operand";

  symex_decl(state, to_symbol_expr(code.op0()));
}

/*******************************************************************\

Function: goto_symext::symex_decl

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_symext::symex_decl(statet &state, const symbol_exprt &expr)
{
  // We increase the L2 renaming to make these non-deterministic.
  // We also prevent propagation of old values.
  
  ssa_exprt ssa(expr);
  state.rename(ssa, ns, goto_symex_statet::L1);

  // rename type to L2
  state.rename(ssa.type(), ssa.get_identifier(), ns);
  ssa.update_type();

  // L2 renaming
  // inlining may yield multiple declarations of the same identifier
  // within the same L1 context
  if(state.level2.current_names.find(l1_identifier)==
     state.level2.current_names.end())
    state.level2.current_names[l1_identifier]=std::make_pair(ssa, 0);

  // skip non-deterministic initialisation if the next instruction
  // will take care of initialisation
  goto_programt::const_targett next=state.source.pc;
  ++next;
  if(next->is_assign() &&
     to_code_assign(next->code).lhs()==expr)
    return;
  
  // we hide the declaration of auxiliary variables
  // and if the statement itself is hidden
  bool hidden=
    ns.lookup(expr.get_identifier()).is_auxiliary ||
    state.top().hidden_function ||
    state.source.pc->source_location.get_hide();
  
  try
  {
    // will fail in case of arrays of non-const size
    null_message_handlert null_message;
    exprt rhs=
      nondet_initializer(
        ssa.type(),
        state.source.pc->source_location,
        ns,
        null_message);
    replace_nondet(rhs);

    guardt guard;
    symex_assign_symbol(
      state,
      ssa,
      nil_exprt(),
      rhs,
      guard,
      hidden?symex_targett::HIDDEN:symex_targett::STATE);
  }
  catch(int)
  {
    exprt l2_fields;
    state.field_sensitivity.get_fields(ns, ssa, l2_fields);
    std::set<exprt> l2_fields_set;
    find_symbols(l2_fields, l2_fields_set);

    for(std::set<exprt>::const_iterator it=l2_fields_set.begin();
        it!=l2_fields_set.end();
        ++it)
    {
      ssa_exprt ssa_lhs=to_ssa_expr(*it);
      const irep_idt &l1_identifier=ssa_lhs.get_identifier();

      // prevent propagation
      state.propagation.remove(l1_identifier);

      // L2 renaming
      // inlining may yield multiple declarations of the same identifier
      // within the same L1 context
      if(state.level2.current_names.find(l1_identifier)==
         state.level2.current_names.end())
        state.level2.current_names[l1_identifier]=std::make_pair(ssa_lhs, 0);
      state.level2.increase_counter(l1_identifier);
      const bool record_events=state.record_events;
      state.record_events=false;
      state.rename(ssa_lhs, ns);
      state.record_events=record_events;

      // record the declaration
      // we hide the declaration of auxiliary variables
      bool hidden=
        ns.lookup(expr.get_identifier()).is_auxiliary ||
        state.top().hidden_function ||
        state.source.pc->source_location.get_hide();
      target.decl(
        state.guard.as_expr(),
        ssa_lhs,
        state.source,
        hidden?symex_targett::HIDDEN:symex_targett::STATE);

      assert(state.dirty);
      if(state.dirty->is_dirty(ssa_lhs.get_object_name()) &&
         state.atomic_section_id==0)
        target.shared_write(
          state.guard.as_expr(),
          ssa_lhs,
          state.atomic_section_id,
          state.source);
    }
  }
}
