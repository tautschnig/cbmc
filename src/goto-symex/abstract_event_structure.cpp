/*******************************************************************\

Module: Collection of read/write events of concurrent programs

Author: Michael Tautschnig, michael.tautschnig@cs.ox.ac.uk

\*******************************************************************/

#include <sstream>

#include <message.h>

#include "abstract_event_structure.h"

#include "partial_order_concurrency.h"
#include "memory_model.h"
#include "symex_target_equation.h"
#include "goto_symex_state.h"

/*******************************************************************\

Function: abstract_eventt::print

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void abstract_eventt::print(std::ostream &os) const
{
  switch(direction)
  {
    case D_WRITE: os << "W"; break;
    case D_READ: os << "R"; break;
    case D_SYNC: os << "SYNC"; return;
    case D_LWSYNC: os << "LWSYNC"; return;
    case D_ISYNC: os << "ISYNC"; return;
  }

  // write or read
  std::string val_str=value.id()==ID_symbol ?
    to_symbol_expr(value).get_identifier().as_string() :
    address.as_string()+"$initval";

  if(val_str.compare(0, 3, "c::")==0)
    os << val_str.substr(3);
  else
    os << val_str;
}

/*******************************************************************\

Function: abstract_events_per_processort::initialize

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void abstract_events_per_processort::initialize(
    const symex_target_equationt &_equation,
    const abstract_events_per_processort * _parent)
{
  equation=&_equation;
  if(_parent)
  {
    if(!_parent->abstract_events.empty())
      parent_event=&(_parent->abstract_events.back());
    else
      parent_event=_parent->parent_event;
  }
}

/*******************************************************************\

Function: abstract_events_per_processort::add_abstract_event

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void abstract_events_per_processort::add_abstract_event(
    const goto_symex_statet &state,
    const abstract_eventt::event_dirt direction,
    const irep_idt &address,
    const exprt &value)
{
  assert(equation);
  assert(abstract_events.empty() ||
      abstract_events.back().source.thread_nr==state.source.thread_nr);

  abstract_events.push_back(
      abstract_eventt(state.guard, state.source,
        direction, address, value,
        state.atomic_section_count, equation->SSA_steps.size()));
}

/*******************************************************************\

Function: abstract_event_structuret::~abstract_event_structuret

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

abstract_event_structuret::~abstract_event_structuret()
{
  if(poc)
    delete poc;
  poc=0;
  if(mm)
    delete mm;
  mm=0;
}

/*******************************************************************\

Function: abstract_event_structuret::add_partial_order_constraints

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void abstract_event_structuret::add_partial_order_constraints(
    const namespacet &ns)
{
  if(abstract_events_in_po.size()<=1)
    return;

  typedef partial_order_concurrencyt::adj_matricest adj_matricest;
  typedef partial_order_concurrencyt::evtt evtt;

  /*if(memory_model=="drf")
    mm=new memory_model_drft();
  else*/ if(memory_model=="sc" || memory_model.empty())
    mm=new memory_model_sct();
  else if(memory_model=="tso")
    mm=new memory_model_tsot();
  else if(memory_model=="pso")
    mm=new memory_model_psot();
  else if(memory_model=="alpha")
    mm=new memory_model_alphat();
  else if(memory_model=="rmo")
    mm=new memory_model_rmot();
  else if(memory_model=="ppc-pldi")
    mm=new memory_model_ppc_pldit();
  else if(memory_model=="arm-pldi")
    mm=new memory_model_arm_pldit();
  else if(memory_model=="power")
    mm=new memory_model_powert();
  else if(memory_model=="arm")
    mm=new memory_model_armt();
  else
    throw "Unsupported memory model";

  poc=new partial_order_concurrencyt(*mm, equation, ns, message);
  poc->init(abstract_events_in_po);

  poc->add_atomic_sections();
  message.debug("Computing PO");
  adj_matricest po;
  poc->add_program_order(po);
  message.debug("Computing RF");
  adj_matricest rf;
  poc->add_read_from(rf);
  message.debug("Computing WS");
  adj_matricest ws;
  poc->add_write_serialisation(ws);
  message.debug("Partial-order constraints before expensive FR/AB:");
  std::ostringstream os_debug;
  for(std::map<std::string, unsigned>::const_iterator
      it=poc->num_concurrency_constraints.begin();
      it!=poc->num_concurrency_constraints.end();
      ++it)
    os_debug << it->first << ": " << it->second << std::endl;
  message.debug(os_debug.str());
  os_debug.clear();
  message.debug("Computing FR");
  adj_matricest fr;
  poc->add_from_read(rf, ws, fr);
  message.debug("Computing AB");
  poc->add_barriers(rf, ws, fr);

  poc->acyclic();

  message.debug("Added partial-order constraints:");
  for(std::map<std::string, unsigned>::const_iterator
      it=poc->num_concurrency_constraints.begin();
      it!=poc->num_concurrency_constraints.end();
      ++it)
    os_debug << it->first << ": " << it->second << std::endl;
  message.debug(os_debug.str());
}

/*******************************************************************\

Function: abstract_event_structuret::build_concrete_event_structure

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void abstract_event_structuret::build_concrete_event_structure(
    const prop_convt &prop_conv,
    concrete_event_structuret &ces) const
{
  if(poc)
    poc->build_concrete_event_structure(prop_conv, ces);
}

