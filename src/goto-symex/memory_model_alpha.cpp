/*******************************************************************\

Module: Memory model for partial order concurrency

Author: Michael Tautschnig, michael.tautschnig@cs.ox.ac.uk

\*******************************************************************/

#include "event_dependencies.h"

#include "memory_model.h"

/*******************************************************************\

Function: memory_model_alphat::~memory_model_alphat

  Inputs: 

 Outputs:

 Purpose:

\*******************************************************************/

memory_model_alphat::~memory_model_alphat()
{
}

/*******************************************************************\

Function: memory_model_alphat::uses_check

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool memory_model_alphat::uses_check(
    partial_order_concurrencyt::acyclict check) const
{
  switch(check)
  {
    case partial_order_concurrencyt::AC_UNIPROC: return true;
    case partial_order_concurrencyt::AC_THINAIR: return true;
    case partial_order_concurrencyt::AC_GHB: return true;
    case partial_order_concurrencyt::AC_PPC_WS_FENCE: return false;
    case partial_order_concurrencyt::AC_N_AXIOMS: assert(false);
  }

  return false;
}

/*******************************************************************\

Function: memory_model_alphat::po_is_relaxed

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool memory_model_alphat::po_is_relaxed(
    partial_order_concurrencyt &poc,
    const evtt &e1,
    const evtt &e2) const
{
  assert(e1.direction==evtt::D_READ || e1.direction==evtt::D_WRITE);
  assert(e2.direction==evtt::D_READ || e2.direction==evtt::D_WRITE);

  // no po relaxation within atomic sections
  if(e1.atomic_sect_id &&
      e1.atomic_sect_id==e2.atomic_sect_id)
    return false;

  // no relaxation if induced wsi
  if(e1.direction==evtt::D_WRITE && e2.direction==evtt::D_WRITE &&
      e1.address==e2.address)
    return false;

  // reads from the same address are maintained
  if(e1.direction==evtt::D_READ && e2.direction==evtt::D_READ &&
      e1.address==e2.address)
    return false;

  event_dependenciest dp(
      poc.get_thread(e2),
      event_dependenciest::MM_ALPHA,
      poc.get_ns(),
      poc.get_message());
  return !dp(e1, e2);
}

