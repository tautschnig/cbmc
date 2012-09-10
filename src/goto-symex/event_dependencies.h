/*******************************************************************\

Module: Dependencies between events for partial order concurrency

Author: Michael Tautschnig, michael.tautschnig@cs.ox.ac.uk

\*******************************************************************/

#ifndef CPROVER_EVENT_DEPENDENCIES_H
#define CPROVER_EVENT_DEPENDENCIES_H

#include <list>

#include "partial_order_concurrency.h"

class event_dependenciest
{
  typedef abstract_eventt evtt;
  typedef std::list<const evtt*> evt_listt;

public:
  // DRF must be the largest number in order to use "<" for comparing one memory
  // model to be weaker than another.
  typedef enum {
    MM_POWER=0,
    MM_ARM_PLDI=1,
    MM_PPC_PLDI=2,
    MM_RMO=3,
    MM_ALPHA=4,
    MM_PSO=5,
    MM_TSO=6,
    MM_SC=7,
    MM_DRF=8
  } memory_modelt;

  event_dependenciest(
      const numbered_evtst &_thread,
      const memory_modelt _memory_model,
      const namespacet &_ns,
      messaget &_message):
    thread(_thread),
    memory_model(_memory_model),
    ns(_ns),
    message(_message)
  {
  }

  bool operator()(
      const evtt &e1,
      const evtt &e2)
  {
    if(memory_model==MM_ARM_PLDI || memory_model==MM_PPC_PLDI)
      return events_in_dp_pldi_ext(e1, e2);
    else
      return events_in_dp(e1, e2);
  }

private:
  const numbered_evtst &thread;
  memory_modelt memory_model;
  const namespacet &ns;
  messaget &message;

  bool events_in_dp(
      const evtt &e1,
      const evtt &e2);

  bool events_in_dp_pldi_ext(
      const evtt &e1,
      const evtt &e2);

  bool events_in_dd(
      const evtt &e1,
      const evtt &e2) const;
  bool events_in_dd(
      const evtt &e1,
      const evtt &e2,
      const bool dp_addr_only) const;
  bool has_dd_ctrl(
      const evtt &e1,
      numbered_evtst::const_iterator e1_entry,
      numbered_evtst::const_iterator e2_entry) const;
  bool events_in_ctrl(
      const evtt &e1,
      numbered_evtst::const_iterator e1_entry,
      const evtt &e2,
      numbered_evtst::const_iterator e2_entry) const;
  bool events_in_isync(
      const evtt &e1,
      numbered_evtst::const_iterator e1_entry,
      numbered_evtst::const_iterator e2_entry) const;
  bool events_in_ctrlisync(
      const evtt &e1,
      numbered_evtst::const_iterator e1_entry,
      numbered_evtst::const_iterator e2_entry) const;
  bool events_in_dd_po_loc(
      const evtt &e1,
      numbered_evtst::const_iterator e1_entry,
      const evtt &e2,
      numbered_evtst::const_iterator e2_entry) const;
  bool events_in_dp_addr(
      const evtt &e1,
      numbered_evtst::const_iterator e1_entry,
      const evtt &e2,
      numbered_evtst::const_iterator e2_entry) const;
  bool events_in_poswr_dpw(
      const evtt &e1,
      numbered_evtst::const_iterator e1_entry,
      const evtt &e2,
      numbered_evtst::const_iterator e2_entry);
  bool events_in_poswr_ctrlisync(
      const evtt &e1,
      numbered_evtst::const_iterator e1_entry,
      const evtt &e2,
      numbered_evtst::const_iterator e2_entry) const;
  bool events_in_dpw_poswr(
      const evtt &e1,
      numbered_evtst::const_iterator e1_entry,
      const evtt &e2,
      numbered_evtst::const_iterator e2_entry) const;
};

#endif
