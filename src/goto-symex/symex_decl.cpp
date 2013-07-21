/*******************************************************************\

Module: Symbolic Execution

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#include <cassert>

#include <util/expr_util.h>
#include <util/rename.h>
#include <util/std_expr.h>
#include <util/message.h>

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
  const irep_idt &l1_identifier=ssa.get_identifier();

  // rename type to L2
  state.rename(ssa.type(), l1_identifier, ns);
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
    // prevent propagation
    state.propagation.remove(l1_identifier);

    state.level2.increase_counter(l1_identifier);
    const bool record_events=state.record_events;
    state.record_events=false;
    state.rename(ssa, ns);
    state.record_events=record_events;

    target.decl(
      state.guard.as_expr(),
      ssa,
      state.source,
      hidden?symex_targett::HIDDEN:symex_targett::STATE);

    assert(state.dirty);
    if(state.dirty->is_dirty(ssa.get_object_name()) &&
       state.atomic_section_id==0)
      target.shared_write(
        state.guard.as_expr(),
        ssa,
        state.atomic_section_id,
        state.source);
  }
}
