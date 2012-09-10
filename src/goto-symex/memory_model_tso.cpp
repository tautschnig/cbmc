/*******************************************************************\

Module: Memory model for partial order concurrency

Author: Michael Tautschnig, michael.tautschnig@cs.ox.ac.uk

\*******************************************************************/

#include "memory_model.h"

/*******************************************************************\

Function: memory_model_tsot::~memory_model_tsot

  Inputs: 

 Outputs:

 Purpose:

\*******************************************************************/

memory_model_tsot::~memory_model_tsot()
{
}

/*******************************************************************\

Function: memory_model_tsot::steps_per_event

  Inputs: 

 Outputs:

 Purpose:

\*******************************************************************/

unsigned memory_model_tsot::steps_per_event(
    const partial_order_concurrencyt &poc,
    evtt::event_dirt direction) const
{
  switch(direction)
  {
    case evtt::D_WRITE: return 1;
    case evtt::D_READ: return 1;
    case evtt::D_LWSYNC: return 2;
    case evtt::D_SYNC: return 1;
    case evtt::D_ISYNC: return 0;
  }

  assert(false);
  return 0;
}

/*******************************************************************\

Function: memory_model_tsot::uses_check

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool memory_model_tsot::uses_check(
    partial_order_concurrencyt::acyclict check) const
{
  switch(check)
  {
    case partial_order_concurrencyt::AC_UNIPROC: return true;
    case partial_order_concurrencyt::AC_THINAIR: return false;
    case partial_order_concurrencyt::AC_GHB: return true;
    case partial_order_concurrencyt::AC_PPC_WS_FENCE: return false;
    case partial_order_concurrencyt::AC_N_AXIOMS: assert(false);
  }

  return false;
}


/*******************************************************************\

Function: memory_model_tsot::po_is_relaxed

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool memory_model_tsot::po_is_relaxed(
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

  // write to read program order is relaxed
  return e1.direction==evtt::D_WRITE && e2.direction==evtt::D_READ;
}

/*******************************************************************\

Function: memory_model_tsot::rf_is_relaxed

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool memory_model_tsot::rf_is_relaxed(
    const evtt &w,
    const evtt &r,
    const bool is_rfi) const
{
  assert(w.direction==evtt::D_WRITE && r.direction==evtt::D_READ);

  return is_rfi;
}

/*******************************************************************\

Function: memory_model_tsot::add_barriers

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void memory_model_tsot::add_barriers(
    partial_order_concurrencyt &poc,
    const numbered_evtst &thread,
    const adj_matricest &rf,
    const adj_matricest &ws,
    const adj_matricest &fr,
    adj_matricest &ab) const
{
  memory_model_baset::add_barriers(poc, thread, rf, ws, fr, ab);
}

