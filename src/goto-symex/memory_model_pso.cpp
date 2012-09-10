/*******************************************************************\

Module: Memory model for partial order concurrency

Author: Michael Tautschnig, michael.tautschnig@cs.ox.ac.uk

\*******************************************************************/

#include "memory_model.h"

/*******************************************************************\

Function: memory_model_psot::~memory_model_psot

  Inputs: 

 Outputs:

 Purpose:

\*******************************************************************/

memory_model_psot::~memory_model_psot()
{
}

/*******************************************************************\

Function: memory_model_psot::po_is_relaxed

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool memory_model_psot::po_is_relaxed(
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

  // write-write to same location is caught by ws
  // read-write and read-read pairs are maintained
  return e1.direction==evtt::D_WRITE;
}

