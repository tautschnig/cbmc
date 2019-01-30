/*******************************************************************\

Module: Symbolic Execution

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

/// \file
/// Symbolic Execution

#include "goto_symex.h"

#include <util/find_symbols.h>
#include <util/std_expr.h>

#include <pointer-analysis/add_failed_symbols.h>

void goto_symext::symex_dead(statet &state)
{
  const goto_programt::instructiont &instruction=*state.source.pc;

  const code_deadt &code = instruction.get_dead();

  ssa_exprt ssa(code.symbol());
  state.rename(ssa, ns, field_sensitivity, goto_symex_statet::L1);

  // in case of pointers, put something into the value set
  if(code.symbol().type().id() == ID_pointer)
  {
    exprt rhs;
    if(auto failed = get_failed_symbol(to_symbol_expr(code.symbol()), ns))
      rhs = address_of_exprt(*failed, to_pointer_type(code.symbol().type()));
    else
      rhs=exprt(ID_invalid);

    state.rename(rhs, ns, field_sensitivity, goto_symex_statet::L1);
    state.value_set.assign(ssa, rhs, ns, true, false);
  }

  const exprt l2_fields = field_sensitivity.get_fields(state, ssa);
  std::set<exprt> l2_fields_set;
  find_symbols(l2_fields, l2_fields_set);

  for(const exprt &l2_field_set : l2_fields_set)
  {
    const irep_idt &l1_identifier = to_ssa_expr(l2_field_set).get_identifier();

    // we cannot remove the object from the L1 renaming map, because L1 renaming
    // information is not local to a path, but removing it from the propagation
    // map is safe as 1) it is local to a path and 2) this instance can no longer
    // appear
    state.propagation.erase(l1_identifier);
    // increment the L2 index to ensure a merge on join points will propagate the
    // value for branches that are still live
    auto level2_it = state.level2.current_names.find(l1_identifier);
    if(level2_it != state.level2.current_names.end())
      symex_renaming_levelt::increase_counter(level2_it);
  }
}
