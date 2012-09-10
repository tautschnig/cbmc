/*******************************************************************\

Module: Memory model for partial order concurrency

Author: Michael Tautschnig, michael.tautschnig@cs.ox.ac.uk

\*******************************************************************/

#include "memory_model.h"

/*******************************************************************\

Function: memory_model_sct::~memory_model_sct

  Inputs: 

 Outputs:

 Purpose:

\*******************************************************************/

memory_model_sct::~memory_model_sct()
{
}

/*******************************************************************\

Function: memory_model_sct::steps_per_event

  Inputs: 

 Outputs:

 Purpose:

\*******************************************************************/

unsigned memory_model_sct::steps_per_event(
    const partial_order_concurrencyt &poc,
    evtt::event_dirt direction) const
{
  switch(direction)
  {
    case evtt::D_WRITE: return 1;
    case evtt::D_READ: return 1;
    case evtt::D_LWSYNC:
    case evtt::D_SYNC:
    case evtt::D_ISYNC: return 0;
  }

  assert(false);
  return 0;
}

/*******************************************************************\

Function: memory_model_sct::add_sub_clock_rules

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void memory_model_sct::add_sub_clock_rules(
    partial_order_concurrencyt &poc) const
{
}

/*******************************************************************\

Function: memory_model_sct::uses_check

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool memory_model_sct::uses_check(
    partial_order_concurrencyt::acyclict check) const
{
  return check==partial_order_concurrencyt::AC_GHB;
}


/*******************************************************************\

Function: memory_model_sct::po_is_relaxed

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool memory_model_sct::po_is_relaxed(
    partial_order_concurrencyt &poc,
    const evtt &e1,
    const evtt &e2) const
{
  return false;
}

/*******************************************************************\

Function: memory_model_sct::rf_is_relaxed

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool memory_model_sct::rf_is_relaxed(
    const evtt &w,
    const evtt &r,
    const bool is_rfi) const
{
  return false;
}

/*******************************************************************\

Function: memory_model_sct::add_atomic_sections

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void memory_model_sct::add_atomic_sections(
    partial_order_concurrencyt &poc) const
{
  memory_model_baset::add_atomic_sections(poc);
}

/*******************************************************************\

Function: memory_model_sct::add_program_order

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void memory_model_sct::add_program_order(
    partial_order_concurrencyt &poc,
    const numbered_evtst &thread,
    adj_matricest &po) const
{
  memory_model_baset::add_program_order(poc, thread, po);
}

/*******************************************************************\

Function: memory_model_sct::add_read_from

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void memory_model_sct::add_read_from(
    partial_order_concurrencyt &poc,
    const partial_order_concurrencyt::per_valuet &reads,
    const partial_order_concurrencyt::per_valuet &writes,
    adj_matricest &rf) const
{
  memory_model_baset::add_read_from(poc, reads, writes, rf);
}

/*******************************************************************\

Function: memory_model_sct::add_write_serialisation_internal

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void memory_model_sct::add_write_serialisation_internal(
    partial_order_concurrencyt &poc,
    const numbered_evtst &thread,
    adj_matricest &ws) const
{
  memory_model_baset::add_write_serialisation_internal(poc, thread, ws);
}

/*******************************************************************\

Function: memory_model_sct::add_write_serialisation_external

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void memory_model_sct::add_write_serialisation_external(
    partial_order_concurrencyt &poc,
    const partial_order_concurrencyt::per_valuet &writes,
    adj_matricest &ws) const
{
  memory_model_baset::add_write_serialisation_external(poc, writes, ws);
}

/*******************************************************************\

Function: memory_model_sct::add_from_read

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void memory_model_sct::add_from_read(
    partial_order_concurrencyt &poc,
    const adj_matricest &rf,
    const adj_matricest &ws,
    adj_matricest &fr) const
{
  memory_model_baset::add_from_read(poc, rf, ws, fr);
}

/*******************************************************************\

Function: memory_model_sct::add_barriers

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void memory_model_sct::add_barriers(
    partial_order_concurrencyt &poc,
    const numbered_evtst &thread,
    const adj_matricest &rf,
    const adj_matricest &ws,
    const adj_matricest &fr,
    adj_matricest &ab) const
{
  // nothing to do
}

