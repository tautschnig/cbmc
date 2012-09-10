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
#define AC_PPC_WS_FENCE partial_order_concurrencyt::AC_PPC_WS_FENCE

/*******************************************************************\

Function: memory_model_ppc_pldit::~memory_model_ppc_pldit

  Inputs: 

 Outputs:

 Purpose:

\*******************************************************************/

memory_model_ppc_pldit::~memory_model_ppc_pldit()
{
}

/*******************************************************************\

Function: memory_model_ppc_pldit::steps_per_event

  Inputs: 

 Outputs:

 Purpose:

\*******************************************************************/

unsigned memory_model_ppc_pldit::steps_per_event(
    const partial_order_concurrencyt &poc,
    evtt::event_dirt direction) const
{
  switch(direction)
  {
    case evtt::D_WRITE: return poc.get_all_threads().size();
    case evtt::D_READ: return 2;
    case evtt::D_LWSYNC: return 2*poc.get_all_threads().size();
    case evtt::D_SYNC: return poc.get_all_threads().size()+1;
    case evtt::D_ISYNC: return 0;
  }

  assert(false);
  return 0;
}

/*******************************************************************\

Function: memory_model_ppc_pldit::add_sub_clock_rules

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void memory_model_ppc_pldit::add_sub_clock_rules(
    partial_order_concurrencyt &poc) const
{
  for(partial_order_concurrencyt::numbered_per_thread_evtst::const_iterator
      it=poc.get_all_threads().begin();
      it!=poc.get_all_threads().end();
      ++it)
  {
    const unsigned cur_thread=it-poc.get_all_threads().begin();

    for(numbered_evtst::const_iterator e_it=it->begin();
        e_it!=it->end();
        ++e_it)
    {
      const evtt &e=**e_it;

      switch(e.direction)
      {
        case evtt::D_READ:
          {
            const symbol_exprt req=poc.clock(AC_GHB, e, S_R_REQ);
            const symbol_exprt c=poc.clock(AC_GHB, e, S_COMMIT);
            binary_relation_exprt lt(req, ID_lt, c);
            poc.add_constraint(lt, guardt(), e.source, "sub-clock-rules");
          }
          break;
        case evtt::D_WRITE:
          {
            const symbol_exprt c=poc.clock(AC_GHB, e, S_COMMIT);
            for(unsigned t=0; t<poc.get_all_threads().size(); ++t)
              if(t!=cur_thread)
              {
                const symbol_exprt p=poc.clock(AC_GHB, e, S_PROP(t));
                binary_relation_exprt lt(c, ID_lt, p);
                poc.add_constraint(lt, guardt(), e.source, "sub-clock-rules");
              }
          }
          break;
        case evtt::D_SYNC:
          {
            const symbol_exprt c=poc.clock(AC_GHB, e, S_COMMIT);
            for(unsigned t=0; t<poc.get_all_threads().size(); ++t)
              if(t!=cur_thread)
              {
                const symbol_exprt p=poc.clock(AC_GHB, e, S_PROP(t));
                binary_relation_exprt lt1(c, ID_lt, p);
                poc.add_constraint(lt1, guardt(), e.source, "sub-clock-rules");
                const symbol_exprt a=poc.clock(AC_GHB, e, S_S_ACK);
                binary_relation_exprt lt2(p, ID_lt, a);
                poc.add_constraint(lt2, guardt(), e.source, "sub-clock-rules");
              }
          }
          break;
        case evtt::D_LWSYNC:
          {
            const symbol_exprt c=poc.clock(AC_GHB, e, S_COMMIT, evtt::D_READ);
            for(unsigned t=0; t<poc.get_all_threads().size(); ++t)
              if(t!=cur_thread)
              {
                const symbol_exprt p=poc.clock(AC_GHB, e, S_PROP(t), evtt::D_READ);
                binary_relation_exprt lt(c, ID_lt, p);
                poc.add_constraint(lt, guardt(), e.source, "sub-clock-rules");
              }
          }
          {
            const symbol_exprt c=poc.clock(AC_GHB, e, S_COMMIT, evtt::D_WRITE);
            for(unsigned t=0; t<poc.get_all_threads().size(); ++t)
              if(t!=cur_thread)
              {
                const symbol_exprt p=poc.clock(AC_GHB, e, S_PROP(t), evtt::D_WRITE);
                binary_relation_exprt lt(c, ID_lt, p);
                poc.add_constraint(lt, guardt(), e.source, "sub-clock-rules");
              }
          }
          break;
        case evtt::D_ISYNC:
          break;
      }
    }
  }
}

/*******************************************************************\

Function: memory_model_ppc_pldit::uses_check

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool memory_model_ppc_pldit::uses_check(
    partial_order_concurrencyt::acyclict check) const
{
  switch(check)
  {
    case partial_order_concurrencyt::AC_UNIPROC: return true;
    case partial_order_concurrencyt::AC_THINAIR: return true;
    case partial_order_concurrencyt::AC_GHB: return true;
    case partial_order_concurrencyt::AC_PPC_WS_FENCE: return true;
    case partial_order_concurrencyt::AC_N_AXIOMS: assert(false);
  }

  return false;
}

/*******************************************************************\

Function: memory_model_ppc_pldit::po_is_relaxed

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool memory_model_ppc_pldit::po_is_relaxed(
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
      event_dependenciest::MM_PPC_PLDI,
      poc.get_ns(),
      poc.get_message());
  return !dp(e1, e2);
}

/*******************************************************************\

Function: memory_model_ppc_pldit::rf_is_relaxed

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool memory_model_ppc_pldit::rf_is_relaxed(
    const evtt &w,
    const evtt &r,
    const bool is_rfi) const
{
  return false;
}

/*******************************************************************\

Function: memory_model_ppc_pldit::add_program_order

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void memory_model_ppc_pldit::add_program_order(
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

      if(e.direction==evtt::D_READ)
        poc.add_partial_order_constraint(
            AC_GHB, "thread-spawn",
            **pred, S_COMMIT, evtt::D_ISYNC,
            e, S_R_REQ, evtt::D_ISYNC, true_exprt());
      else
        poc.add_partial_order_constraint(
            AC_GHB, "thread-spawn", **pred, e, true_exprt());
      po[AC_GHB][*pred][&e].make_true();
    }

    // uniproc -- program order per address
    if(uses_check(AC_UNIPROC))
    {
      partial_order_concurrencyt::per_valuet &mr=most_recent_evt[e.address];
      if(!mr.empty())
      {
        poc.add_partial_order_constraint(
            AC_UNIPROC, "uniproc", *mr.back(), e, true_exprt());
        po[AC_UNIPROC][mr.back()][&e].make_true();
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
          if(e.direction==evtt::D_READ)
          {
            if(c_e.direction==evtt::D_WRITE)
              poc.add_partial_order_constraint(AC_GHB, "po",
                  c_e, S_COMMIT, evtt::D_ISYNC,
                  e, S_R_REQ, evtt::D_ISYNC, true_exprt());
            else
              poc.add_partial_order_constraint(AC_GHB, "po",
                  c_e, S_R_REQ, evtt::D_ISYNC,
                  e, S_R_REQ, evtt::D_ISYNC, true_exprt());
          }
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

/*******************************************************************\

Function: write_symbol_primed

  Inputs: 

 Outputs:

 Purpose:

\*******************************************************************/

static exprt write_symbol_primed(
    const partial_order_concurrencyt::evtt &evt)
{
  assert(evt.direction==partial_order_concurrencyt::evtt::D_WRITE);

  if(evt.value.id()!=ID_symbol)
  {
    // initialisation
    assert(evt.guard.is_true());
    return evt.value;
  }

  const std::string name=
    to_symbol_expr(evt.value).get_identifier().as_string() + "$val";

  return symbol_exprt(name, evt.value.type());
}

/*******************************************************************\

Function: memory_model_ppc_pldit::add_read_from

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void memory_model_ppc_pldit::add_read_from(
    partial_order_concurrencyt &poc,
    const partial_order_concurrencyt::per_valuet &reads,
    const partial_order_concurrencyt::per_valuet &writes,
    adj_matricest &rf) const
{
  for(partial_order_concurrencyt::per_valuet::const_iterator
      it=reads.begin();
      it!=reads.end();
      ++it)
  {
    const evtt &r_evt=**it;
    exprt::operandst rf_some_operands;
    rf_some_operands.reserve(writes.size());

    for(partial_order_concurrencyt::per_valuet::const_iterator
        it2=writes.begin();
        it2!=writes.end();
        ++it2)
    {
      const evtt &w_evt=**it2;

      // rf cannot contradict program order
      const evtt* f_e=poc.first_of(r_evt, w_evt);
      bool is_rfi=f_e!=0;
      if(is_rfi && f_e==&r_evt)
        continue; // contradicts po

      // only read from the most recent write, extra wsi constraints ensure
      // that even a write with guard false will have the proper value
      if(is_rfi)
      {
        numbered_evtst::const_iterator w_entry=
          poc.get_thread(r_evt).find(w_evt);
        assert(w_entry!=poc.get_thread(r_evt).end());
        numbered_evtst::const_iterator r_entry=
          poc.get_thread(r_evt).find(r_evt);
        assert(r_entry!=poc.get_thread(r_evt).end());

        assert(w_entry<r_entry);
        bool is_most_recent=true;
        for(++w_entry; w_entry!=r_entry && is_most_recent; ++w_entry)
          is_most_recent&=(*w_entry)->direction!=evtt::D_WRITE ||
            (*w_entry)->address!=r_evt.address;
        if(!is_most_recent)
          continue;
      }

      symbol_exprt s=poc.fresh_nondet_bool();
      implies_exprt read_from(s,
          and_exprt((is_rfi ? true_exprt() : w_evt.guard.as_expr()),
            equal_exprt(r_evt.value, write_symbol_primed(w_evt))));
      poc.add_constraint(read_from, guardt(), r_evt.source, "rf");

      rf_some_operands.push_back(s);


      // uniproc, thinair and ghb orders are in sync via s
      if(uses_check(AC_UNIPROC))
      {
        poc.add_partial_order_constraint(AC_UNIPROC, "rf", w_evt, r_evt, s);
        // write-to-read edge
        rf[AC_UNIPROC][&w_evt].insert(std::make_pair(&r_evt, s));
      }
      if(uses_check(AC_THINAIR))
      {
        poc.add_partial_order_constraint(AC_THINAIR, "rf", w_evt, r_evt, s);
        // write-to-read edge
        rf[AC_THINAIR][&w_evt].insert(std::make_pair(&r_evt, s));
      }
      if(!rf_is_relaxed(w_evt, r_evt, is_rfi))
        poc.add_partial_order_constraint(AC_GHB, "rf",
            w_evt, S_PROP(r_evt.source.thread_nr), evtt::D_ISYNC,
            r_evt, S_R_REQ, evtt::D_ISYNC, s);
      // write-to-read edge
      rf[AC_GHB][&w_evt].insert(std::make_pair(&r_evt, s));
    }

    // value equals one of some write
    or_exprt rf_some;
    rf_some.operands().swap(rf_some_operands);
    poc.add_constraint(rf_some, r_evt.guard, r_evt.source, "rf-at-least-one");
  }
}

/*******************************************************************\

Function: memory_model_ppc_pldit::add_write_serialisation_external

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void memory_model_ppc_pldit::add_write_serialisation_external(
    partial_order_concurrencyt &poc,
    const partial_order_concurrencyt::per_valuet &writes,
    adj_matricest &ws) const
{
  for(partial_order_concurrencyt::per_valuet::const_iterator
      it=writes.begin();
      it!=writes.end();
      ++it)
  {
    const evtt &w_evt1=**it;

    partial_order_concurrencyt::per_valuet::const_iterator next=it;
    ++next;
    for(partial_order_concurrencyt::per_valuet::const_iterator
        it2=next;
        it2!=writes.end();
        ++it2)
    {
      const evtt &w_evt2=**it2;

      // only wse here
      if(poc.first_of(w_evt1, w_evt2)!=0)
        continue;

      // ws is a total order, no two elements have the same rank
      // s -> w_evt1 before w_evt2; !s -> w_evt2 before w_evt1
      symbol_exprt s=poc.fresh_nondet_bool();

      // write-to-write edge
      ws[AC_GHB][&w_evt1].insert(std::make_pair(&w_evt2, s));
      ws[AC_GHB][&w_evt2].insert(std::make_pair(&w_evt1, not_exprt(s)));

      poc.add_partial_order_constraint(AC_GHB, "ws",
          w_evt1, S_COMMIT, evtt::D_ISYNC,
          w_evt2, S_PROP(w_evt1.source.thread_nr), evtt::D_ISYNC, s);
      poc.add_partial_order_constraint(AC_GHB, "ws",
          w_evt2, S_COMMIT, evtt::D_ISYNC,
          w_evt1, S_PROP(w_evt2.source.thread_nr), evtt::D_ISYNC, not_exprt(s));

      if(uses_check(AC_UNIPROC))
      {
        poc.add_partial_order_constraint(AC_UNIPROC, "ws", w_evt1, w_evt2, s);
        poc.add_partial_order_constraint(AC_UNIPROC, "ws", w_evt2, w_evt1,
            not_exprt(s));
        // write-to-write edge
        ws[AC_UNIPROC][&w_evt1].insert(std::make_pair(&w_evt2, s));
        ws[AC_UNIPROC][&w_evt2].insert(std::make_pair(&w_evt1, not_exprt(s)));
      }

      // PLDI requires acyclicity of writes and fences
      if(uses_check(AC_PPC_WS_FENCE))
      {
        poc.add_partial_order_constraint(AC_PPC_WS_FENCE, "ws",
            w_evt1, w_evt2, s);
        poc.add_partial_order_constraint(AC_PPC_WS_FENCE, "ws",
            w_evt2, w_evt1, not_exprt(s));
      }
    }
  }
}

/*******************************************************************\

Function: memory_model_ppc_pldit::add_from_read

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void memory_model_ppc_pldit::add_from_read(
      partial_order_concurrencyt &poc,
      const partial_order_concurrencyt::acyclict check,
      const partial_order_concurrencyt::adj_matrixt &rf,
      const partial_order_concurrencyt::adj_matrixt &ws,
      partial_order_concurrencyt::adj_matrixt &fr) const
{
  assert(check==AC_GHB || check==AC_UNIPROC);

  // from-read: (w', w) in ws and (w', r) in rf -> (r, w) in fr
  // uniproc and ghb orders are guaranteed to be in sync via the
  // underlying orders rf and ws
  for(partial_order_concurrencyt::adj_matrixt::const_iterator
      w_prime=ws.begin();
      w_prime!=ws.end();
      ++w_prime)
  {
    partial_order_concurrencyt::adj_matrixt::const_iterator w_prime_rf=
      rf.find(w_prime->first);
    if(w_prime_rf==rf.end())
      continue;

    for(std::map<evtt const*, exprt>::const_iterator
        r=w_prime_rf->second.begin();
        r!=w_prime_rf->second.end();
        ++r)
    {
      const evtt &r_evt=*(r->first);

      for(std::map<evtt const*, exprt>::const_iterator
          w=w_prime->second.begin();
          w!=w_prime->second.end();
          ++w)
      {
        const evtt &w_evt=*(w->first);

        const evtt* f_e=poc.first_of(r_evt, w_evt);
        bool is_fri=f_e!=0;
        // TODO: make sure the following skips are ok even if guard does not
        // evaluate to true
        // internal fr only backward or to first successor (in po)
        if(check==AC_GHB)
        {
          numbered_evtst::const_iterator w_entry=
            poc.get_thread(w_evt).find(w_evt);
          assert(w_entry!=poc.get_thread(w_evt).end());
          numbered_evtst::const_iterator r_entry=
            poc.get_thread(w_evt).find(r_evt);

          if(r_entry!=poc.get_thread(w_evt).end() &&
              r_entry<w_entry)
          {
            bool is_next=true;
            for(++r_entry; r_entry!=w_entry && is_next; ++r_entry)
              is_next&=(*r_entry)->direction!=evtt::D_WRITE ||
                (*r_entry)->address!=w_evt.address;
            if(!is_next)
              continue;
          }
        }
        // no internal forward fr, these are redundant with po-loc
        else if(check==AC_UNIPROC && is_fri && f_e==&r_evt)
          continue;

        and_exprt a(and_exprt(r->second, w->second), w_evt.guard.as_expr());
        if(is_fri || check!=AC_GHB)
          poc.add_partial_order_constraint(check, "fr", r_evt, w_evt, a);
        else
          poc.add_partial_order_constraint(check, "fr",
              r_evt, S_R_REQ, evtt::D_ISYNC,
              w_evt, S_PROP(r_evt.source.thread_nr), evtt::D_ISYNC, a);
        // read-to-write edge
        std::pair<std::map<evtt const*, exprt>::iterator, bool> fr_map_entry=
          fr[&r_evt].insert(std::make_pair(&w_evt, a));
        if(!fr_map_entry.second)
        {
          or_exprt o(fr_map_entry.first->second, a);
          fr_map_entry.first->second.swap(o);
        }
      }
    }
  }
}

/*******************************************************************\

Function: partial_order_concurrencyt::add_from_read

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void memory_model_ppc_pldit::add_from_read(
    partial_order_concurrencyt &poc,
    const adj_matricest &rf,
    const adj_matricest &ws,
    adj_matricest &fr) const
{
  add_from_read(poc, AC_GHB, rf[AC_GHB], ws[AC_GHB], fr[AC_GHB]);
  if(uses_check(AC_UNIPROC))
    add_from_read(poc, AC_UNIPROC,
        rf[AC_UNIPROC], ws[AC_UNIPROC], fr[AC_UNIPROC]);
}

/*******************************************************************\

Function: memory_model_ppc_pldit::add_barriers

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void memory_model_ppc_pldit::add_barriers(
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

  partial_order_concurrencyt::adj_matrixt fr_reverse;
  for(partial_order_concurrencyt::adj_matrixt::const_iterator
      r=fr[AC_GHB].begin();
      r!=fr[AC_GHB].end();
      ++r)
    for(std::map<evtt const*, exprt>::const_iterator
        w=r->second.begin();
        w!=r->second.end();
        ++w)
      fr_reverse[w->first].insert(std::make_pair(r->first, w->second));

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
      {
        if(!before_barrier)
        {
          if((*e_it)->direction==evtt::D_READ)
            poc.add_partial_order_constraint(AC_GHB, "ab",
                ***b_it, S_S_ACK, evtt::D_ISYNC,
                **e_it, S_R_REQ, evtt::D_ISYNC, guard);
          else
            poc.add_partial_order_constraint(AC_GHB, "ab",
                ***b_it, S_S_ACK, evtt::D_ISYNC,
                **e_it, S_COMMIT, evtt::D_ISYNC, guard);
        }
        else
        {
          if((*e_it)->direction==evtt::D_READ)
            poc.add_partial_order_constraint(AC_GHB, "ab",
                **e_it, S_COMMIT, evtt::D_ISYNC,
                ***b_it, S_COMMIT, evtt::D_ISYNC, guard);
          else
          {
            poc.add_partial_order_constraint(AC_GHB, "ab",
                **e_it, ***b_it, guard);
            for(unsigned t=0; t<poc.get_all_threads().size(); ++t)
              if(t!=(*e_it)->source.thread_nr)
                poc.add_partial_order_constraint(AC_GHB, "ab",
                    **e_it, S_PROP(t), evtt::D_ISYNC,
                    ***b_it, S_PROP(t), evtt::D_ISYNC, guard);
          }
        }
      }
      else
      {
        if(!before_barrier)
        {
          if((*e_it)->direction==evtt::D_READ)
            poc.add_partial_order_constraint(AC_GHB, "ab",
                ***b_it, S_COMMIT, evtt::D_READ,
                **e_it, S_COMMIT, evtt::D_READ, guard);
          else
          {
            poc.add_partial_order_constraint(AC_GHB, "ab",
                ***b_it, S_COMMIT, evtt::D_WRITE,
                **e_it, S_COMMIT, evtt::D_WRITE, guard);
            poc.add_partial_order_constraint(AC_GHB, "ab",
                ***b_it, S_COMMIT, evtt::D_READ,
                **e_it, S_COMMIT, evtt::D_READ, guard);
            for(unsigned t=0; t<poc.get_all_threads().size(); ++t)
              if(t!=(*e_it)->source.thread_nr)
              {
                poc.add_partial_order_constraint(AC_GHB, "ab",
                    ***b_it, S_PROP(t), evtt::D_WRITE,
                    **e_it, S_PROP(t), evtt::D_WRITE, guard);
                poc.add_partial_order_constraint(AC_GHB, "ab",
                    ***b_it, S_PROP(t), evtt::D_READ,
                    **e_it, S_PROP(t), evtt::D_READ, guard);
              }
          }
        }
        else
        {
          if((*e_it)->direction==evtt::D_READ)
          {
            poc.add_partial_order_constraint(AC_GHB, "ab",
                **e_it, S_COMMIT, evtt::D_READ,
                ***b_it, S_COMMIT, evtt::D_READ, guard);
            for(numbered_evtst::const_iterator r_it=*b_it;
                r_it!=thread.end();
                ++r_it)
              if((*r_it)->direction==evtt::D_READ)
                poc.add_partial_order_constraint(AC_GHB, "ab",
                    **e_it, S_COMMIT, evtt::D_ISYNC,
                    **r_it, S_R_REQ, evtt::D_ISYNC, guard);
          }
          else
          {
            poc.add_partial_order_constraint(AC_GHB, "ab",
                **e_it, S_COMMIT, evtt::D_WRITE,
                ***b_it, S_COMMIT, evtt::D_WRITE, guard);
            for(unsigned t=0; t<poc.get_all_threads().size(); ++t)
              if(t!=(*e_it)->source.thread_nr)
                poc.add_partial_order_constraint(AC_GHB, "ab",
                    **e_it, S_PROP(t), evtt::D_WRITE,
                    ***b_it, S_PROP(t), evtt::D_WRITE, guard);
          }
        }
      }

      if((*e_it)->direction==evtt::D_WRITE)
        poc.add_partial_order_constraint(AC_PPC_WS_FENCE, "ab",
            *first_e, S_COMMIT, evtt::D_WRITE,
            *second_e, S_COMMIT, evtt::D_WRITE, guard);

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
            for(unsigned t=0; t<poc.get_all_threads().size(); ++t)
              if(t!=r_evt.source.thread_nr &&
                  t!=w_evt.source.thread_nr)
                poc.add_partial_order_constraint(AC_GHB, "ab",
                    w_evt, S_PROP(t), evtt::D_WRITE,
                    ***b_it, S_PROP(t), evtt::D_WRITE, a);

            ab[AC_GHB][&w_evt].insert(std::make_pair(**b_it, a));

            poc.add_partial_order_constraint(AC_PPC_WS_FENCE, "ab",
                w_evt, S_COMMIT, evtt::D_WRITE,
                ***b_it, S_COMMIT, evtt::D_WRITE, guard);

            // sync;fre;rfe;sync
            partial_order_concurrencyt::adj_matrixt::const_iterator fr_r_entry=
              fr_reverse.find(&w_evt);
            if((**b_it)->direction==evtt::D_SYNC && fr_r_entry!=fr_reverse.end())
            {
              for(std::map<evtt const*, exprt>::const_iterator
                  r=fr_r_entry->second.begin();
                  r!=fr_r_entry->second.end();
                  ++r)
              {
                and_exprt fr_a(r->second, a);

                const numbered_evtst &r_thread=poc.get_thread(*(r->first));
                numbered_evtst::const_iterator dp_entry=r_thread.find(*(r->first));
                assert(dp_entry!=r_thread.end());
                while(dp_entry!=r_thread.begin())
                  if((*(--dp_entry))->direction==evtt::D_SYNC)
                    poc.add_partial_order_constraint(AC_PPC_WS_FENCE, "ab",
                        **dp_entry, S_COMMIT, evtt::D_WRITE,
                        ***b_it, S_COMMIT, evtt::D_WRITE, fr_a);
              }
            }
          }
        }

        // sync;fre;sync
        if((**b_it)->direction==evtt::D_SYNC)
        {
          partial_order_concurrencyt::adj_matrixt::const_iterator fr_r_entry=
            fr_reverse.find(&r_evt);
          if(fr_r_entry!=fr_reverse.end())
          {
            for(std::map<evtt const*, exprt>::const_iterator
                r=fr_r_entry->second.begin();
                r!=fr_r_entry->second.end();
                ++r)
            {
              and_exprt a(r->second, guard);

              const numbered_evtst &r_thread=poc.get_thread(*(r->first));
              numbered_evtst::const_iterator dp_entry=r_thread.find(*(r->first));
              assert(dp_entry!=r_thread.end());
              while(dp_entry!=r_thread.begin())
                if((*(--dp_entry))->direction==evtt::D_SYNC)
                  poc.add_partial_order_constraint(AC_PPC_WS_FENCE, "ab",
                      **dp_entry, S_COMMIT, evtt::D_WRITE,
                      ***b_it, S_COMMIT, evtt::D_WRITE, a);
            }
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

            // (lw)sync;rfe;dpW ((lw)sync;rfe;(lw)sync by A-cumulativity)
            const numbered_evtst &r_thread=poc.get_thread(r_evt);
            numbered_evtst::const_iterator dp_entry=r_thread.find(r_evt);
            assert(dp_entry!=r_thread.end());
            partial_order_concurrencyt::per_valuet dps(1, *dp_entry);
            event_dependenciest dp(
                r_thread,
                event_dependenciest::MM_PPC_PLDI,
                poc.get_ns(),
                poc.get_message());
            for(++dp_entry; dp_entry!=r_thread.end(); ++dp_entry)
              if((*dp_entry)->direction==evtt::D_WRITE ||
                  (*dp_entry)->direction==evtt::D_READ)
              {
                bool dp_found=false;
                for(partial_order_concurrencyt::per_valuet::const_iterator
                    it_d=dps.begin();
                    !dp_found && it_d!=dps.end();
                    ++it_d)
                  dp_found=dp(**it_d, **dp_entry);
                if(dp_found)
                  dps.push_back(*dp_entry);
              }
            for(partial_order_concurrencyt::per_valuet::const_iterator
                it_d=dps.begin();
                it_d!=dps.end();
                ++it_d)
              if((*it_d)->direction==evtt::D_WRITE)
              {
                for(unsigned t=0; t<poc.get_all_threads().size(); ++t)
                  if(t!=(**b_it)->source.thread_nr &&
                      t!=(*it_d)->source.thread_nr)
                  {
                    poc.add_partial_order_constraint(AC_GHB, "ab",
                        ***b_it, S_PROP(t), evtt::D_WRITE,
                        **it_d, S_PROP(t), evtt::D_WRITE, a);
                    poc.add_partial_order_constraint(AC_GHB, "ab",
                        ***b_it, S_PROP(t), evtt::D_READ,
                        **it_d, S_PROP(t), evtt::D_READ, a);
                  }
                ab[AC_GHB][**b_it].insert(std::make_pair(*it_d, a));

                poc.add_partial_order_constraint(AC_PPC_WS_FENCE, "ab",
                    ***b_it, S_COMMIT, evtt::D_WRITE,
                    **it_d, S_COMMIT, evtt::D_WRITE, a);
              }
          }
        }
      }
    }
  }
}

