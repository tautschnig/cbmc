/*******************************************************************\

Module: Dependencies between events for partial order concurrency

Author: Michael Tautschnig, michael.tautschnig@cs.ox.ac.uk

\*******************************************************************/

#include <cassert>
#include <set>
#include <map>
#include <sstream>

#include <std_expr.h>
#include <message.h>

#include "event_dependencies.h"

#define DUMMY_FOR_SEMICOLON ((void)0)
#define DP_DEBUG(name, e1, e2) \
  { \
    std::ostringstream oss; \
    oss << "DP " << name << " " << e1 << " -> " << e2; \
    message.debug(oss.str()); \
  } DUMMY_FOR_SEMICOLON

/*******************************************************************\

Function: collect_symbols

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

static void collect_symbols(
    const exprt &expr,
    std::set<exprt const *> &symbols)
{
	if (expr.id()==ID_symbol)
		symbols.insert(&expr);
	else {
		forall_operands(iter, expr)
      collect_symbols(*iter, symbols);
	}
}

/*******************************************************************\

Function: collect_expr_with_parents

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

static void collect_expr_with_parents(
    const exprt &expr,
    std::map<exprt const*, exprt const*> &exprs)
{
	exprs.insert(
      std::make_pair<exprt const*, exprt const*>(&expr, 0));
	forall_operands(iter, expr) {
			exprs.insert(std::make_pair(&(*iter), &expr));
			collect_expr_with_parents(*iter, exprs);
	}
}

/*******************************************************************\

Function: collect_parents

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

static void collect_parents(
    const symbol_exprt &sym,
    goto_programt::const_targett pc,
    std::set<exprt const*> &parents)
{
  std::map<exprt const*, exprt const*> subexprt_to_parents;
  collect_expr_with_parents(pc->code, subexprt_to_parents);
  std::set<exprt const*> contained_symbols;
  collect_symbols(pc->code, contained_symbols);

  std::set<exprt const*> dereferenced;
  for(std::set<exprt const*>::const_iterator
      it=contained_symbols.begin();
      it!=contained_symbols.end();
      ++it)
  {
    const irep_idt &identifier=to_symbol_expr(**it).get_identifier();
    for(exprt const* parent=*it; parent!=0; )
    {
      if(identifier==sym.get_identifier())
        parents.insert(parent);
      else if(parent->id()==ID_dereference)
        dereferenced.insert(parent);

      std::map<exprt const*, exprt const*>::const_iterator
        entry=subexprt_to_parents.find(parent);
      assert(entry!=subexprt_to_parents.end());
      parent=entry->second;
    }
  }
  if(parents.empty())
    parents.swap(dereferenced);
}

/*******************************************************************\

Function: get_depended_on

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

static void get_depended_on(
    const symbol_exprt &sym,
    goto_programt::const_targett pc,
    const bool with_addr_deps,
    const bool with_data_deps,
    std::set<exprt const *> &deps,
    std::set<irep_idt> &killed)
{
  std::set<exprt const*> parents;
  collect_parents(sym, pc, parents);

  std::set<exprt const*> lhs;
  for(std::set<exprt const*>::const_iterator it=parents.begin();
      it!=parents.end();
      ++it)
  {
    if(with_addr_deps && (*it)->id()==ID_index)
      // it might be necessary to check whether e1 occurs as array and e2 as
      // index; we skip this for now
      collect_symbols(**it, deps);
    else if(with_addr_deps && (*it)->id()==ID_dereference)
      collect_symbols(**it, deps);
    else if(with_data_deps && ((*it)->id()==ID_assign ||
          (*it)->id()==ID_code && to_code(**it).get_statement()==ID_assign) &&
        parents.find(&((*it)->op0()))!=parents.end())
    {
      collect_symbols((*it)->op1(), deps);
      collect_symbols((*it)->op0(), lhs);
    }
  }
  for(std::set<exprt const*>::const_iterator it=lhs.begin();
      it!=lhs.end();
      ++it)
    killed.insert(to_symbol_expr(**it).get_identifier());
  for(std::set<exprt const*>::const_iterator it=deps.begin();
      it!=deps.end();
      ++it)
  {
    std::set<irep_idt>::iterator k_entry=
      killed.find(to_symbol_expr(**it).get_identifier());
    if(k_entry!=killed.end())
      killed.erase(k_entry);
  }
}

/*******************************************************************\

Function: get_dependencies_at

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

static void get_dependencies_at(
    const symbol_exprt &e2_sym,
    goto_programt::const_targett e2_pc,
    goto_programt::const_targett e1_pc,
    const bool dp_addr_only,
    const bool recurse,
    const namespacet &ns,
    std::set<exprt const *> &deps_at_pc)
{
  assert(deps_at_pc.empty());

  // we only do function-local dependencies -- may be an overapproximation
  if(e1_pc->function!=e2_pc->function) return;

  // make sure we can linearly get from e1's pc to e2's pc -- another
  // overapproximation
  if(e1_pc->location_number>e2_pc->location_number) return;

  // collect dependencies of e2_sym at e2_pc and then recursively collect
  // dependencies live at e1_pc
  std::set<exprt const*> all_deps;
  std::set<irep_idt> killed_at_pc;
  get_depended_on(e2_sym, e2_pc, true, !dp_addr_only,
      deps_at_pc, killed_at_pc);

  for(goto_programt::const_targett it=e2_pc;
      it!=e1_pc;
      --it)
  {
    assert(it->location_number>e1_pc->location_number);

    goto_programt::const_targett pred=it;
    --pred;

    all_deps.insert(deps_at_pc.begin(), deps_at_pc.end());
    deps_at_pc.clear();
    std::set<irep_idt> killed_at_pc_bak;
    killed_at_pc_bak.swap(killed_at_pc);
    for(std::set<exprt const*>::iterator it_d=all_deps.begin();
        it_d!=all_deps.end();
        ) // no ++it_d
    {
      const symbol_exprt &sym=to_symbol_expr(**it_d);
      if(killed_at_pc_bak.find(sym.get_identifier())!=
          killed_at_pc_bak.end() ||
          (!recurse && is_global(ns.lookup(sym.get_identifier()))))
      {
        std::set<exprt const*>::iterator next=it_d;
        ++next;
        all_deps.erase(it_d);
        it_d=next;
      }
      else
      {
        ++it_d;
        get_depended_on(sym, pred, false, true, deps_at_pc, killed_at_pc);
      }
    }
  }
}

/*******************************************************************\

Function: get_dependencies_at

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

static void get_dependencies_at(
    const abstract_eventt &e2,
    goto_programt::const_targett e1_pc,
    const bool dp_addr_only,
    const bool recurse,
    const namespacet &ns,
    std::set<exprt const *> &deps_at_pc)
{
  symbol_exprt e2_sym(e2.address, e2.value.type());
  get_dependencies_at(e2_sym, e2.source.pc, e1_pc, dp_addr_only, recurse,
      ns, deps_at_pc);
}

/*******************************************************************\

Function: event_dependenciest::events_in_dd

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool event_dependenciest::events_in_dd(
    const evtt &e1,
    const evtt &e2) const
{
  return events_in_dd(e1, e2, false);
}

/*******************************************************************\

Function: event_dependenciest::events_in_dd

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool event_dependenciest::events_in_dd(
    const evtt &e1,
    const evtt &e2,
    const bool dp_addr_only) const
{
  // source must be a read
  if(e1.direction!=evtt::D_READ)
    return false;

  // ssa_index is more fine grained than e.source.pc as it obeys loop
  // unwindings; catch a simple case first
  if(!dp_addr_only &&
      e1.ssa_index==e2.ssa_index &&
      e1.source.pc==e2.source.pc &&
      e2.direction==evtt::D_WRITE)
    return true;

  // figure out in what form e2 occurs in its source expression and collect its
  // dependencies
  std::set<exprt const *> deps_at_pc;
  get_dependencies_at(e2, e1.source.pc, dp_addr_only,
      memory_model!=MM_ARM_PLDI, ns, deps_at_pc);
  // check whether e1 is in dependencies
  for(std::set<exprt const*>::const_iterator it=deps_at_pc.begin();
      it!=deps_at_pc.end();
      ++it)
    if(to_symbol_expr(**it).get_identifier()==e1.address)
      return true;

  return false;
}

/*******************************************************************\

Function: event_dependenciest::has_dd_ctrl

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool event_dependenciest::has_dd_ctrl(
    const evtt &e1,
    numbered_evtst::const_iterator e1_entry,
    numbered_evtst::const_iterator e2_entry) const
{
  // works for commit events on shared variables only, locals left as TODO; to
  // address this, we would need to collect all guards added program-order after
  // e1_entry (up to e2_entry) to any instruction, and then apply
  // get_dependencies_at(sym, guard_occ, e1.source.pc, false, ...) to all
  // symbols occurring in these guards; ideally we wouldn't have to rely on goto
  // programs, but would instead be using all SSA entries between e1_entry and
  // e2_entry to easily work interprocedurally

  // actually we would have to account for guards, but this is left as a TODO
  // to solve this problem, all events_in_* functions would have to return an
  // exprt that contains the guard, and ppo would become condtional
  assert(e1_entry<e2_entry);

  if(e1.source.pc->is_goto())
    return true;

  evt_listt commit_read_candidates;
  for( ; e1_entry!=e2_entry; --e2_entry)
    switch((*e2_entry)->direction)
    {
      case evtt::D_SYNC:
      case evtt::D_LWSYNC:
      case evtt::D_ISYNC:
        break;
      case evtt::D_READ:
        if((*e2_entry)->source.pc->is_goto())
          commit_read_candidates.push_back(*e2_entry);
        break;
      // as reads into registers manifest as writes, we need to look at those
      // instead of directly doing events_in_dd on *e2_entry inserted into
      // commit_read_candidates
      case evtt::D_WRITE:
        for(evt_listt::iterator it=commit_read_candidates.begin();
            it!=commit_read_candidates.end();
           ) // no ++it
          if((*e2_entry)->address==(*it)->address)
          {
            it=commit_read_candidates.erase(it);
            if(events_in_dd(e1, **e2_entry))
              return true;
          }
          else
            ++it;
        break;
    }

  return false;
}

/*******************************************************************\

Function: event_dependenciest::events_in_ctrl

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool event_dependenciest::events_in_ctrl(
    const evtt &e1,
    numbered_evtst::const_iterator e1_entry,
    const evtt &e2,
    numbered_evtst::const_iterator e2_entry) const
{
  return e2.direction==evtt::D_WRITE && has_dd_ctrl(e1, e1_entry, e2_entry);
}

/*******************************************************************\

Function: event_dependenciest::events_in_isync

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool event_dependenciest::events_in_isync(
    const evtt &e1,
    numbered_evtst::const_iterator e1_entry,
    numbered_evtst::const_iterator e2_entry) const
{
  // actually we would have to account for guards, but this is left as a TODO
  // to solve this problem, all events_in_* functions would have to return an
  // exprt that contains the guard, and ppo would become condtional
  assert(e1_entry<e2_entry);

  bool isync_seen=false;
  for( ; e1_entry!=e2_entry; --e2_entry)
    switch((*e2_entry)->direction)
    {
      case evtt::D_SYNC:
      case evtt::D_LWSYNC:
        break;
      case evtt::D_ISYNC:
        isync_seen=true;
        break;
      case evtt::D_READ:
      case evtt::D_WRITE:
        if(isync_seen && events_in_dd(e1, **e2_entry, true))
          return true;
        break;
    }

  return false;
}

/*******************************************************************\

Function: event_dependenciest::events_in_ctrlisync

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool event_dependenciest::events_in_ctrlisync(
    const evtt &e1,
    numbered_evtst::const_iterator e1_entry,
    numbered_evtst::const_iterator e2_entry) const
{
  // actually we would have to account for guards, but this is left as a TODO
  // to solve this problem, all events_in_* functions would have to return an
  // exprt that contains the guard, and ppo would become condtional
  assert(e1_entry<e2_entry);

  bool isync_seen=false;
  for( ; e1_entry!=e2_entry; --e2_entry)
    switch((*e2_entry)->direction)
    {
      case evtt::D_SYNC:
      case evtt::D_LWSYNC:
      case evtt::D_WRITE:
        break;
      case evtt::D_ISYNC:
        isync_seen=true;
        break;
      case evtt::D_READ:
        if(isync_seen && has_dd_ctrl(e1, e1_entry, e2_entry))
          return true;
        break;
    }

  // catch case e1;isync;e2 with e1 itself being ctrl
  return isync_seen && e1.source.pc->is_goto();
}

/*******************************************************************\

Function: event_dependenciest::events_in_dd_po_loc

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool event_dependenciest::events_in_dd_po_loc(
    const evtt &e1,
    numbered_evtst::const_iterator e1_entry,
    const evtt &e2,
    numbered_evtst::const_iterator e2_entry) const
{
  if(e2.direction!=evtt::D_READ && memory_model!=MM_ALPHA)
    return false;

  assert(e1_entry<e2_entry);
  // uses a possibly debatable RMO specific extension in that it includes ctrl
  const bool mm_rmo=memory_model==MM_RMO;
  // also cater for alpha's processor issue constraint
  const bool mm_alpha=memory_model==MM_ALPHA;
  for( ; e1_entry!=e2_entry; --e2_entry)
    if((mm_alpha || (*e2_entry)->direction==evtt::D_WRITE) &&
        (*e2_entry)->address==e2.address &&
        (events_in_dd(e1, **e2_entry) ||
         ((mm_rmo || mm_alpha) &&
          events_in_ctrl(e1, e1_entry, **e2_entry, e2_entry))))
      return true;

  return false;
}

/*******************************************************************\

Function: event_dependenciest::events_in_dp

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool event_dependenciest::events_in_dp(
    const evtt &e1,
    const evtt &e2)
{
  // we don't need dp for any model at least as strong as PSO
  if(memory_model>=MM_PSO)
    return false;

  // source must be a read
  if(e1.direction!=evtt::D_READ)
    return false;

  // we don't consider fences as events here
  if(e2.direction!=evtt::D_READ &&
      e2.direction!=evtt::D_WRITE)
    return false;

  DP_DEBUG("?", e1, e2);

  if(events_in_dd(e1, e2))
  {
    DP_DEBUG("dd", e1, e2);
    return true;
  }

  numbered_evtst::const_iterator e1_entry=thread.find(e1),
    e2_entry=thread.find(e2);
  assert(e1_entry!=thread.end());
  assert(e2_entry!=thread.end());

  if(events_in_ctrl(e1, e1_entry, e2, e2_entry))
  {
    DP_DEBUG("ctrl", e1, e2);
    return true;
  }

  if((memory_model==MM_POWER || memory_model==MM_PPC_PLDI) &&
      events_in_isync(e1, e1_entry, e2_entry))
  {
    DP_DEBUG("isync", e1, e2);
    return true;
  }

  if((memory_model==MM_POWER || memory_model==MM_PPC_PLDI ||
        memory_model==MM_ARM_PLDI) &&
      events_in_ctrlisync(e1, e1_entry, e2_entry))
  {
    DP_DEBUG("ctrl-isync", e1, e2);
    return true;
  }

  if(memory_model!=MM_PPC_PLDI && memory_model!=MM_ARM_PLDI &&
      events_in_dd_po_loc(e1, e1_entry, e2, e2_entry))
  {
    DP_DEBUG("dd-po-loc", e1, e2);
    return true;
  }

  return false;
}

/*******************************************************************\

Function: event_dependenciest::events_in_dp_addr

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool event_dependenciest::events_in_dp_addr(
    const evtt &e1,
    numbered_evtst::const_iterator e1_entry,
    const evtt &e2,
    numbered_evtst::const_iterator e2_entry) const
{
  assert(e1_entry<e2_entry);
  for( ; e1_entry!=e2_entry; --e2_entry)
    if(events_in_dd(e1, **e2_entry, true))
      return true;

  return false;
}

/*******************************************************************\

Function: event_dependenciest::events_in_poswr_dpw

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool event_dependenciest::events_in_poswr_dpw(
    const evtt &e1,
    numbered_evtst::const_iterator e1_entry,
    const evtt &e2,
    numbered_evtst::const_iterator e2_entry)
{
  // source and dest must be a write
  if(e1.direction!=evtt::D_WRITE ||
      e2.direction!=evtt::D_WRITE)
    return false;

  assert(e1_entry<e2_entry);

  evt_listt dps;
  for(++e1_entry; e1_entry!=e2_entry; ++e1_entry)
    if((*e1_entry)->direction==evtt::D_READ)
    {
      if(e1.address==(*e1_entry)->address)
        dps.push_back(*e1_entry);
      else
        for(evt_listt::const_iterator it_d=dps.begin();
            it_d!=dps.end();
            ++it_d)
          if(events_in_dp(**it_d, **e1_entry))
            dps.push_front(*e1_entry);
    }

  for(evt_listt::const_iterator it_d=dps.begin();
      it_d!=dps.end();
      ++it_d)
    if(events_in_dp_pldi_ext(**it_d, e2))
      return true;

  return false;
}

/*******************************************************************\

Function: event_dependenciest::events_in_poswr_ctrlisync

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool event_dependenciest::events_in_poswr_ctrlisync(
    const evtt &e1,
    numbered_evtst::const_iterator e1_entry,
    const evtt &e2,
    numbered_evtst::const_iterator e2_entry) const
{
  // source must be a write
  if(e1.direction!=evtt::D_WRITE)
    return false;

  assert(e1_entry<e2_entry);
  for(++e1_entry; e1_entry!=e2_entry; ++e1_entry)
    if(e1.address==(*e1_entry)->address &&
        events_in_ctrlisync(**e1_entry, e1_entry, e2_entry))
      return true;

  return false;
}

/*******************************************************************\

Function: event_dependenciest::events_in_dpw_poswr

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool event_dependenciest::events_in_dpw_poswr(
    const evtt &e1,
    numbered_evtst::const_iterator e1_entry,
    const evtt &e2,
    numbered_evtst::const_iterator e2_entry) const
{
  // source and dest must be a read
  if(e1.direction!=evtt::D_READ ||
      e2.direction!=evtt::D_READ)
    return false;

  assert(e1_entry<e2_entry);
  for(--e2_entry; e2_entry!=e1_entry; --e2_entry)
    if((*e2_entry)->address==e2.address &&
        (*e2_entry)->direction==evtt::D_WRITE)
      return events_in_dd(e1, **e2_entry);

  return false;
}

/*******************************************************************\

Function: event_dependenciest::events_in_dp_pldi_ext

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool event_dependenciest::events_in_dp_pldi_ext(
    const evtt &e1,
    const evtt &e2)
{
  DP_DEBUG("pldi?", e1, e2);

  if(e1.address==e2.address && e2.direction==evtt::D_WRITE)
  {
    DP_DEBUG("pos*W", e1, e2);
    return true;
  }

  numbered_evtst::const_iterator e1_entry=thread.find(e1),
    e2_entry=thread.find(e2);
  assert(e1_entry!=thread.end());
  assert(e2_entry!=thread.end());

  if(events_in_dpw_poswr(e1, e1_entry, e2, e2_entry))
  {
    DP_DEBUG("dpW-poswr", e1, e2);
    return true;
  }

  if(events_in_poswr_dpw(e1, e1_entry, e2, e2_entry))
  {
    DP_DEBUG("poswr-dpW", e1, e2);
    return true;
  }

  if(memory_model==MM_PPC_PLDI &&
      events_in_poswr_ctrlisync(e1, e1_entry, e2, e2_entry))
  {
    DP_DEBUG("poswr-dpR-isync", e1, e2);
    return true;
  }

  if(e2.direction==evtt::D_WRITE &&
      events_in_dp_addr(e1, e1_entry, e2, e2_entry))
  {
    DP_DEBUG("DpAddrW", e1, e2);
    return true;
  }

  return events_in_dp(e1, e2);
}

