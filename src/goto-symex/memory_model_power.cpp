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

Function: memory_model_powert::~memory_model_powert

  Inputs: 

 Outputs:

 Purpose:

\*******************************************************************/

memory_model_powert::~memory_model_powert()
{
}

/*******************************************************************\

Function: memory_model_powert::po_is_relaxed

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool memory_model_powert::po_is_relaxed(
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
      event_dependenciest::MM_POWER,
      poc.get_ns(),
      poc.get_message());
  return !dp(e1, e2);
}

/*******************************************************************\

Function: memory_model_powert::rf_is_relaxed

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool memory_model_powert::rf_is_relaxed(
    const evtt &w,
    const evtt &r,
    const bool is_rfi) const
{
  return true;
}

/*******************************************************************\

Function: memory_model_powert::add_barriers

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void memory_model_powert::add_barriers(
    partial_order_concurrencyt &poc,
    const numbered_evtst &thread,
    const adj_matricest &rf,
    const adj_matricest &ws,
    const adj_matricest &fr,
    adj_matricest &ab) const
{
  // e1 -[lw]sync-> e2 --> e1 -ab-> e2  plus A/B cumulativity
  const std::list<numbered_evtst::const_iterator> barriers=
    thread.barriers_after(**(thread.begin()));
  if(barriers.empty())
    return;

  partial_order_concurrencyt::adj_matrixt rf_reverse;
  for(partial_order_concurrencyt::adj_matrixt::const_iterator
      w=rf[AC_GHB].begin();
      w!=rf[AC_GHB].end();
      ++w)
    for(std::map<evtt const*, exprt>::const_iterator
        r=w->second.begin();
        r!=w->second.end();
        ++r)
      rf_reverse[r->first].insert(std::make_pair(w->first, r->second));

  for(numbered_evtst::const_iterator e_it=thread.begin();
      e_it!=thread.end();
      ++e_it)
  {
    if((*e_it)->direction!=evtt::D_READ &&
        (*e_it)->direction!=evtt::D_WRITE)
      continue;

    for(std::list<numbered_evtst::const_iterator>::const_iterator
        b_it=barriers.begin();
        b_it!=barriers.end();
        ++b_it)
    {
      const bool before_barrier=e_it<*b_it;
      assert(before_barrier || e_it>*b_it);
      const evtt * first_e=before_barrier ? *e_it : **b_it;
      const evtt * second_e=before_barrier ? **b_it : *e_it;
      const exprt guard=(**b_it)->guard.as_expr();

      if((**b_it)->direction==evtt::D_SYNC)
        poc.add_partial_order_constraint(
            AC_GHB, "ab", *first_e, *second_e, guard);
      else
      {
        assert((**b_it)->direction==evtt::D_LWSYNC);

        if((*e_it)->direction==evtt::D_WRITE && !before_barrier)
          poc.add_partial_order_constraint(
              AC_GHB, "ab", *first_e, S_COMMIT, evtt::D_WRITE,
              *second_e, S_COMMIT, evtt::D_WRITE, guard);

        poc.add_partial_order_constraint(
            AC_GHB, "ab", *first_e, S_COMMIT, evtt::D_READ,
            *second_e, S_COMMIT, first_e->direction, guard);
      }

      ab[AC_GHB][first_e].insert(std::make_pair(second_e, guard));

      // A-cumulativity
      if(before_barrier)
      {
        const evtt &r_evt=**e_it;
        partial_order_concurrencyt::adj_matrixt::const_iterator rf_r_entry=
          rf_reverse.find(&r_evt);
        if(rf_r_entry!=rf_reverse.end())
        {
          for(std::map<evtt const*, exprt>::const_iterator
              w_it=rf_r_entry->second.begin();
              w_it!=rf_r_entry->second.end();
              ++w_it)
          {
            const evtt &w_evt=*(w_it->first);
            if(thread.find(w_evt)!=thread.end())
              continue;

            and_exprt a(w_it->second, guard);
            poc.add_partial_order_constraint(AC_GHB, "ab",
                w_evt, S_COMMIT, evtt::D_WRITE,
                ***b_it, S_COMMIT, evtt::D_WRITE, a);

            ab[AC_GHB][&w_evt].insert(std::make_pair(**b_it, a));
          }
        }
      }
      // B-cumulativity
      else if((*e_it)->direction==evtt::D_WRITE)
      {
        const evtt &w_evt=**e_it;
        partial_order_concurrencyt::adj_matrixt::const_iterator rf_entry=
          rf[AC_GHB].find(&w_evt);
        if(rf_entry!=rf[AC_GHB].end())
        {
          for(std::map<evtt const*, exprt>::const_iterator
              r_it=rf_entry->second.begin();
              r_it!=rf_entry->second.end();
              ++r_it)
          {
            const evtt &r_evt=*(r_it->first);
            if(thread.find(r_evt)!=thread.end())
              continue;

            and_exprt a(r_it->second, guard);
            poc.add_partial_order_constraint(AC_GHB, "ab",
                ***b_it, S_COMMIT, evtt::D_READ,
                r_evt, S_COMMIT, evtt::D_READ, a);

            ab[AC_GHB][**b_it].insert(std::make_pair(&r_evt, a));
          }
        }
      }
    }
  }
}

