/*******************************************************************\

Module: Add constraints to equation encoding partial orders on events

Author: Michael Tautschnig, michael.tautschnig@cs.ox.ac.uk

\*******************************************************************/

/// \file
/// Add constraints to equation encoding partial orders on events

#include "partial_order_concurrency.h"

#include <util/arith_tools.h>
#include <util/bitvector_types.h>
#include <util/expr_util.h>
#include <util/simplify_expr.h>

#include <unordered_set>

partial_order_concurrencyt::partial_order_concurrencyt(
  const namespacet &_ns):ns(_ns)
{
}

partial_order_concurrencyt::~partial_order_concurrencyt()
{
}

void partial_order_concurrencyt::add_init_writes(
  symex_target_equationt &equation)
{
  std::unordered_set<irep_idt> init_done;
  bool spawn_seen=false;

  symex_target_equationt::SSA_stepst init_steps;

  for(eventst::const_iterator
      e_it=equation.SSA_steps.begin();
      e_it!=equation.SSA_steps.end();
      e_it++)
  {
    if(e_it->is_spawn())
    {
      spawn_seen=true;
      continue;
    }
    else if(!e_it->is_shared_read() &&
            !e_it->is_shared_write())
      continue;

    const irep_idt &a=address(e_it);

    if(init_done.find(a)!=init_done.end())
      continue;

    if(spawn_seen || e_it->is_shared_read() || e_it->guard != true)
    {
      init_steps.emplace_back(
        e_it->source, goto_trace_stept::typet::SHARED_WRITE);
      SSA_stept &SSA_step = init_steps.back();

      SSA_step.guard=true_exprt();
      // no SSA L2 index, thus nondet value
      SSA_step.ssa_lhs = remove_level_2(e_it->ssa_lhs);
      SSA_step.atomic_section_id=0;
    }

    init_done.insert(a);
  }

  equation.SSA_steps.splice(equation.SSA_steps.begin(), init_steps);
}

void partial_order_concurrencyt::build_event_lists(
  symex_target_equationt &equation,
  message_handlert &message_handler)
{
  add_init_writes(equation);

  // The addresses that some event reads. A write to another address is not
  // an event: no read can observe it, and leaving it out of the program
  // order keeps the order of the remaining events (see build_event_lists in
  // partial_order_concurrency.h).
  std::unordered_set<irep_idt> read_addresses;
  for(const auto &step : equation.SSA_steps)
  {
    if(step.is_shared_read())
      read_addresses.insert(remove_level_2(step.ssa_lhs).get_identifier());
  }

  // a per-thread counter
  std::map<unsigned, unsigned> counter;
  std::size_t unread_writes = 0;

  for(eventst::const_iterator
      e_it=equation.SSA_steps.begin();
      e_it!=equation.SSA_steps.end();
      e_it++)
  {
    if(
      e_it->is_shared_write() &&
      read_addresses.find(address(e_it)) == read_addresses.end())
    {
      ++unread_writes;
      continue;
    }

    if(e_it->is_shared_read() ||
       e_it->is_shared_write() ||
       e_it->is_spawn())
    {
      unsigned thread_nr=e_it->source.thread_nr;

      if(!e_it->is_spawn())
      {
        a_rect &a_rec=address_map[address(e_it)];

        if(e_it->is_shared_read())
          a_rec.reads.push_back(e_it);
        else // must be write
          a_rec.writes.push_back(e_it);
      }

      // maps an event id to a per-thread counter
      unsigned cnt=counter[thread_nr]++;
      numbering[e_it]=cnt;
    }
  }

  messaget log{message_handler};
  log.statistics() << "Shared writes that no event reads: " << unread_writes
                   << messaget::eom;
  for(address_mapt::const_iterator
      a_it=address_map.begin();
      a_it!=address_map.end();
      a_it++)
  {
    const a_rect &a_rec=a_it->second;
    if(a_rec.reads.empty())
      continue;

    log.statistics() << "Shared " << a_it->first << ": " << a_rec.reads.size()
                     << "R/" << a_rec.writes.size() << "W" << messaget::eom;
  }
}

irep_idt partial_order_concurrencyt::rw_clock_id(
  event_it event,
  axiomt axiom)
{
  if(event->is_shared_write())
    return id2string(id(event))+"$wclk$"+std::to_string(axiom);
  else if(event->is_shared_read())
    return id2string(id(event))+"$rclk$"+std::to_string(axiom);
  else
    UNREACHABLE;
}

symbol_exprt partial_order_concurrencyt::clock(
  event_it event,
  axiomt axiom)
{
  PRECONDITION(!numbering.empty());
  irep_idt identifier;

  if(event->is_shared_write())
    identifier=rw_clock_id(event, axiom);
  else if(event->is_shared_read())
    identifier=rw_clock_id(event, axiom);
  else if(event->is_spawn())
  {
    identifier=
      "t"+std::to_string(event->source.thread_nr+1)+"$"+
      std::to_string(numbering[event])+"$spwnclk$"+std::to_string(axiom);
  }
  else
    UNREACHABLE;

  return symbol_exprt(identifier, clock_type);
}

void partial_order_concurrencyt::build_clock_type()
{
  PRECONDITION(!numbering.empty());

  std::size_t width = address_bits(numbering.size());
  clock_type = unsignedbv_typet(width);
}

exprt partial_order_concurrencyt::before(
  event_it e1, event_it e2, unsigned axioms)
{
  const axiomt axiom_bits[]=
  {
    AX_SC_PER_LOCATION,
    AX_NO_THINAIR,
    AX_OBSERVATION,
    AX_PROPAGATION
  };

  exprt::operandst ops;
  ops.reserve(sizeof(axiom_bits)/sizeof(axiomt));

  for(int i=0; i<int(sizeof(axiom_bits)/sizeof(axiomt)); ++i)
  {
    const axiomt ax=axiom_bits[i];

    if((axioms &ax)==0)
      continue;

    if(e1->atomic_section_id!=0 &&
       e1->atomic_section_id==e2->atomic_section_id)
      ops.push_back(equal_exprt(clock(e1, ax), clock(e2, ax)));
    else
      ops.push_back(
        binary_relation_exprt(clock(e1, ax), ID_lt, clock(e2, ax)));
  }

  POSTCONDITION(!ops.empty());

  return conjunction(ops);
}

void partial_order_concurrencyt::add_constraint(
  symex_target_equationt &equation,
  const exprt &cond,
  const std::string &msg,
  const symex_targett::sourcet &source) const
{
  exprt tmp=cond;
  simplify(tmp, ns);

  equation.constraint(tmp, msg, source);
}

void partial_order_concurrencyt::add_implication(
  symex_target_equationt &equation,
  const exprt::operandst &premises,
  const exprt &consequence,
  const std::string &msg,
  const symex_targett::sourcet &source) const
{
  if(auto constraint = implication(premises, consequence))
    equation.constraint(*constraint, msg, source);
}

std::optional<exprt> partial_order_concurrencyt::implication(
  const exprt::operandst &premises,
  const exprt &consequence)
{
  if(consequence.is_true())
    return {};
  // The conjunction of the premises, flattened, without duplicates (the
  // guards of two events often share conjuncts) and constants; a false or
  // contradictory conjunction makes the implication hold.
  exprt::operandst ops;
  std::unordered_set<exprt, irep_hash> seen;
  std::vector<const exprt *> work;
  for(auto it = premises.rbegin(); it != premises.rend(); ++it)
    work.push_back(&*it);
  while(!work.empty())
  {
    const exprt &premise = *work.back();
    work.pop_back();
    if(premise.id() == ID_and)
    {
      for(auto it = premise.operands().rbegin();
          it != premise.operands().rend();
          ++it)
      {
        work.push_back(&*it);
      }
      continue;
    }
    if(premise.is_false())
      return {};
    if(premise.is_true() || !seen.insert(premise).second)
      continue;
    if(seen.count(boolean_negate(premise)) != 0)
      return {};
    ops.push_back(premise);
  }
  // As the simplifier would: !p1 || ... || !pn || consequence.
  exprt::operandst disjuncts;
  disjuncts.reserve(ops.size() + 1);
  for(const exprt &op : ops)
    disjuncts.push_back(boolean_negate(op));
  if(consequence.id() == ID_or)
  {
    for(const exprt &op : consequence.operands())
      disjuncts.push_back(op);
  }
  else if(!consequence.is_false())
    disjuncts.push_back(consequence);
  return disjunction(disjuncts);
}
