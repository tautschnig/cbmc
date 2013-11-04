/*******************************************************************\

Module: Memory model for partial order concurrency

Author: Michael Tautschnig, michael.tautschnig@cs.ox.ac.uk

\*******************************************************************/

#include <util/std_expr.h>
#include <util/i2string.h>
#include <util/guard.h>

#include "memory_model.h"

/*******************************************************************\

Function: memory_model_baset::memory_model_baset

  Inputs: 

 Outputs:

 Purpose:

\*******************************************************************/

memory_model_baset::memory_model_baset(const namespacet &_ns):
  partial_order_concurrencyt(_ns),
  var_cnt(0)
{
}

/*******************************************************************\

Function: memory_model_baset::~memory_model_baset

  Inputs: 

 Outputs:

 Purpose:

\*******************************************************************/

memory_model_baset::~memory_model_baset()
{
}

/*******************************************************************\

Function: memory_model_baset::nondet_bool_symbol

  Inputs: 

 Outputs:

 Purpose:

\*******************************************************************/

symbol_exprt memory_model_baset::nondet_bool_symbol(
  const std::string &prefix)
{
  return symbol_exprt(
    "memory_model::choice_"+prefix+i2string(var_cnt++),
    bool_typet());
}

/*******************************************************************\

Function: memory_model_baset::po

  Inputs: 

 Outputs:

 Purpose:

\*******************************************************************/

bool memory_model_baset::po(event_it e1, event_it e2)
{
  // within same thread
  if(e1->source.thread_nr==e2->source.thread_nr)
    return numbering[e1]<numbering[e2];
  else
  {
    // in general un-ordered, with exception of thread-spawning
    return false;
  }
}

/*******************************************************************\

Function: memory_model_baset::is_overwritten

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool memory_model_baset::is_overwritten(
  event_it read,
  event_listt::const_iterator w_it,
  const event_listt &writes)
{
  // It may be worth looking into the approach proposed by Chaki et al.
  // in "Efficient Verification of Periodic Programs using Sequential
  // Consistency and Snapshots," FMCAD 2014. This would work as
  // follows, and instead of is_overwritten and friends:
  // 1. In symex_target_equationtt::shared_write do at the end:
  // a) (if atomic_section_id==0) atomic_begin(-1)
  // b) shared_read
  // c) shared_write
  // d) (if atomic_section_id==0) atomic_end(-1)
  // 2. In various memory_model* functions, do not consider external
  // writes in atomic section -1 (to be considered again)
  // 3. Limit rfi to most recent write, and only those inside atomic
  // sections.

  event_it w1=*w_it;

  if(!po(w1, read))
    return false;

  for(++w_it; w_it!=writes.end(); ++w_it)
  {
    event_it w2=*w_it;

    if(!po(w2, read))
      return false;

    // assuming the read's guard, will w2 always manifest when w1 does?
    // simulate
    // or_exprt o(not_exprt(read->guard), not_exprt(w1->guard), w2->guard);
    // which simplify can't nicely deal with
    guardt w2_guard;
    w2_guard.add(w2->guard);
    guardt read_guard;
    read_guard.add(read->guard);
    guardt w1_guard;
    w1_guard.add(w1->guard);

    read_guard|=w1_guard;
    w2_guard-=read_guard;

    if(w2_guard.is_true())
      return true;
  }

  return false;
}

/*******************************************************************\

Function: memory_model_baset::read_from

  Inputs: 

 Outputs:

 Purpose:

\*******************************************************************/

void memory_model_baset::read_from(symex_target_equationt &equation)
{
  // We iterate over all the reads, and
  // make them match at least one
  // (internal or external) write.

  for(address_mapt::const_iterator
      a_it=address_map.begin();
      a_it!=address_map.end();
      a_it++)
  {
    const a_rect &a_rec=a_it->second;
  
    for(event_listt::const_iterator
        r_it=a_rec.reads.begin();
        r_it!=a_rec.reads.end();
        r_it++)
    {
      const event_it r=*r_it;
      
      exprt::operandst rf_some_operands;
      rf_some_operands.reserve(a_rec.writes.size());

      // this is quadratic in #events per address
      for(event_listt::const_iterator
          w_it=a_rec.writes.begin();
          w_it!=a_rec.writes.end();
          ++w_it)
      {
        const event_it w=*w_it;
        
        // rf cannot contradict program order
        if(po(r, w))
          continue; // contradicts po

        bool is_rfi=
          w->source.thread_nr==r->source.thread_nr;

        // see whether we could ever read from this write,
        // or would necessarily read from a later one
        if(is_rfi &&
           is_overwritten(r, w_it, a_rec.writes))
          continue;

        symbol_exprt s=nondet_bool_symbol("rf");
        
        // record the symbol
        choice_symbols[
          std::make_pair(r, w)]=s;

        // We rely on the fact that there is at least
        // one write event that has guard 'true'.
        implies_exprt read_from(s,
            and_exprt(w->guard,
              equal_exprt(r->ssa_lhs, w->ssa_lhs)));

        // Uses only the write's guard as precondition, read's guard
        // follows from rf_some
        add_constraint(equation,
          read_from, is_rfi?"rfi":"rf", r->source);

        if(!is_rfi)
        {
          // if r reads from w, then w must have happened before r
          exprt cond=implies_exprt(s, before(w, r));
          add_constraint(equation,
            cond, "rf-order", r->source);
        }

        rf_some_operands.push_back(s);
      }
      
      // value equals the one of some write
      exprt rf_some;

      // uninitialised global symbol like symex_dynamic::dynamic_object*
      // or *$object
      if(rf_some_operands.empty())
        continue;
      else if(rf_some_operands.size()==1)
        rf_some=rf_some_operands.front();
      else
      {
        rf_some=or_exprt();
        rf_some.operands().swap(rf_some_operands);
      }

      // Add the read's guard, each of the writes' guards is implied
      // by each entry in rf_some
      add_constraint(equation,
        implies_exprt(r->guard, rf_some), "rf-some", r->source);
    }
  }
}

