/*******************************************************************\

Module: Memory model for partial order concurrency

Author: Michael Tautschnig, michael.tautschnig@cs.ox.ac.uk

\*******************************************************************/

#include <std_expr.h>

#include "event_dependencies.h"

#include "memory_model.h"

#define AC_UNIPROC partial_order_concurrencyt::AC_UNIPROC
#define AC_THINAIR partial_order_concurrencyt::AC_THINAIR
#define AC_GHB partial_order_concurrencyt::AC_GHB

/*******************************************************************\

Function: memory_model_armt::~memory_model_armt

  Inputs: 

 Outputs:

 Purpose:

\*******************************************************************/

memory_model_armt::~memory_model_armt()
{
}

/*******************************************************************\

Function: memory_model_armt::po_is_relaxed

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool memory_model_armt::po_is_relaxed(
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

  event_dependenciest dp(
      poc.get_thread(e2),
      event_dependenciest::MM_ARM_PLDI,
      poc.get_ns(),
      poc.get_message());
  return !dp(e1, e2);
}

/*******************************************************************\

Function: memory_model_armt::add_program_order

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void memory_model_armt::add_program_order(
    partial_order_concurrencyt &poc,
    const numbered_evtst &thread,
    adj_matricest &po) const
{
  assert(thread.begin()!=thread.end());

  partial_order_concurrencyt::per_address_mapt most_recent_evt;
  typedef std::list<numbered_evtst::const_iterator> chaint;
  typedef std::list<chaint> root_chainst;

  numbered_evtst::const_iterator pred=thread.begin();
  assert((*pred)->direction==evtt::D_READ ||
      (*pred)->direction==evtt::D_WRITE);
  most_recent_evt[(*pred)->address].push_back(*pred);
  root_chainst root_chains(1, chaint(1, pred));
  for(numbered_evtst::const_iterator e_it=++(thread.begin());
      e_it!=thread.end();
      ++e_it)
  {
    const evtt &e=**e_it;

    // check e for non-barrier event; pred updated below, hence pred is always
    // a non-barrier
    if(e.direction!=evtt::D_READ && e.direction!=evtt::D_WRITE)
      continue;

    // check for thread spawn -- in po with last event before spawn
    if(e.source.thread_nr!=(*pred)->source.thread_nr)
    {
      assert(e.source.thread_nr>(*pred)->source.thread_nr);

      poc.add_partial_order_constraint(
          AC_GHB, "thread-spawn", **pred, e, true_exprt());
      po[AC_GHB][*pred][&e].make_true();
    }

    // uniproc -- program order per address
    if(uses_check(AC_UNIPROC))
    {
      partial_order_concurrencyt::per_valuet &mr=most_recent_evt[e.address];
      partial_order_concurrencyt::per_valuet po_loc_pred;
      // RMO and ARM permit read-read reordering
      if(!mr.empty() && mr.back()->direction==evtt::D_READ)
      {
        // should be const_reverse_iterator, but some broken STL lack operator!=
        // for those
        for(partial_order_concurrencyt::per_valuet::reverse_iterator
            r_it=mr.rbegin();
            r_it!=mr.rend();
            ++r_it)
        {
          if(e.direction==evtt::D_READ &&
              (*r_it)->direction==evtt::D_WRITE)
            po_loc_pred.push_back(*r_it);
          else if(e.direction==evtt::D_WRITE &&
              (*r_it)->direction==evtt::D_READ)
            po_loc_pred.push_back(*r_it);

          if((*r_it)->direction==evtt::D_WRITE)
            break;
        }
      }
      else if(!mr.empty())
        po_loc_pred.push_back(mr.back());
      for(partial_order_concurrencyt::per_valuet::const_iterator
          mr_it=po_loc_pred.begin();
          mr_it!=po_loc_pred.end();
          ++mr_it)
      {
        poc.add_partial_order_constraint(
            AC_UNIPROC, "uniproc", **mr_it, e, true_exprt());
        po[AC_UNIPROC][*mr_it][&e].make_true();
      }
      mr.push_back(&e);
    }

    // program order
    bool extended_chain=false;
    for(root_chainst::iterator r_it=root_chains.begin();
        r_it!=root_chains.end();
        ++r_it)
    {
      // should be const_reverse_iterator, but some STLs are buggy
      for(chaint::reverse_iterator cand=r_it->rbegin();
          cand!=r_it->rend();
          ++cand)
      {
        const evtt &c_e=***cand;
        // dependencies
        if(!po_is_relaxed(poc, c_e, e))
        {
          if(uses_check(AC_THINAIR))
          {
            poc.add_partial_order_constraint(
                AC_THINAIR, "thin-air", c_e, e, true_exprt());
            po[AC_THINAIR][&c_e][&e].make_true();
          }

          poc.add_partial_order_constraint(
              AC_GHB, "po", c_e, e, true_exprt());
          po[AC_GHB][&c_e][&e].make_true();

          if(cand==r_it->rbegin())
          {
            r_it->push_back(e_it);
            extended_chain=true;
          }
          break;
        }
      }
    }
    if(!extended_chain)
      root_chains.push_back(chaint(1, e_it));

    pred=e_it;
  }
}

