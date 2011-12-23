/*******************************************************************\

Module: Invariant Instrumentation

Author: Michael Tautschnig, Daniel Kroening

Date: December 2011

\*******************************************************************/

#include <util/cprover_prefix.h>
#include <util/symbol_table.h>

#include <goto-programs/goto_functions.h>

#include "check_invariant.h"
#include "rw_set.h"

/*******************************************************************\

Function: wr_intersect

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool wr_intersect(
  const rw_set_baset &rw_set,
  const rw_set_baset &invariant_rw_set)
{
  forall_rw_set_w_entries(e_it, rw_set)
  {
    if(invariant_rw_set.has_r_entry(e_it->first))
      return true;
  }

  return false;
}

/*******************************************************************\

Function: invariant

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void invariant(
  value_setst &value_sets,
  const symbol_tablet &symbol_table,
  goto_programt &goto_program,
  const symbol_exprt &invariant_check,
  const rw_set_baset &invariant_rw_set)
{
  namespacet ns(symbol_table);
  
  Forall_goto_program_instructions(i_it, goto_program)
  {
    goto_programt::instructiont &instruction=*i_it;

    rw_set_loct rw_set(ns, value_sets, i_it);

    if(!wr_intersect(rw_set, invariant_rw_set))
      continue;
    
    assert(instruction.is_assign()); // otherwise W set must be empty

    // insert the call to the invariant check
    const locationt &location=i_it->location;
    const irep_idt &function=i_it->function;
   
    code_function_callt call;
    call.location()=location;
    call.function()=invariant_check;

    i_it=goto_program.insert_before(++i_it);
    i_it->make_function_call(call);
    i_it->location=location;
    i_it->function=function;
  }
}

/*******************************************************************\

Function: get_invariant

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

symbol_exprt get_invariant(
  const symbol_tablet &symbol_table,
  const irep_idt &invariant_check)
{
  std::list<symbol_exprt> matches;

  forall_symbol_base_map(m_it, symbol_table.symbol_base_map, invariant_check)
  {
    // look it up
    symbol_tablet::symbolst::const_iterator s_it=
      symbol_table.symbols.find(m_it->second);
    
    if(s_it==symbol_table.symbols.end()) continue;
  
    if(s_it->second.type.id()==ID_code)
      matches.push_back(s_it->second.symbol_expr());
  }
  
  if(matches.empty())
    throw "invariant check `"+id2string(invariant_check)+"' not found";

  if(matches.size()>=2)
    throw "invariant check `"+id2string(invariant_check)+"' is ambiguous";

  symbol_exprt invariant=matches.front();
  
  if(!to_code_type(invariant.type()).arguments().empty())
    throw "invariant check `"+id2string(invariant_check)+
          "' must not have arguments";

  return invariant;
}

/*******************************************************************\

Function: invariant

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void invariant(
  value_setst &value_sets,
  const symbol_tablet &symbol_table,
  goto_functionst &goto_functions,
  const irep_idt &invariant_check)
{
  // look up the invariant check
  symbol_exprt inv=get_invariant(symbol_table, invariant_check);

  // we first figure out which objects are read by the invariant check
  namespacet ns(symbol_table);
  rw_set_functiont invariant_rw_set(
    value_sets, ns, goto_functions, inv);

  // now instrument

  Forall_goto_functions(f_it, goto_functions)
    if(f_it->first!=CPROVER_PREFIX "initialize" &&
       f_it->first!=ID_main &&
       f_it->first!=invariant_check)
      invariant(
        value_sets, symbol_table, f_it->second.body, inv, invariant_rw_set);

  goto_functions.update();
}

