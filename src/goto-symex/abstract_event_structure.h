/*******************************************************************\

Module: Collection of read/write events of concurrent programs

Author: Michael Tautschnig, michael.tautschnig@cs.ox.ac.uk

\*******************************************************************/

#ifndef CPROVER_ABSTRACT_EVENT_STRUCTURE_H
#define CPROVER_ABSTRACT_EVENT_STRUCTURE_H

#include <string>
#include <list>
#include <ostream>

#include <guard.h>
#include "symex_target.h"

class prop_convt;
class concrete_event_structuret;
class goto_symex_statet;
class symex_target_equationt;
class partial_order_concurrencyt;
class memory_model_baset;
class messaget;

class abstract_eventt
{
public:
  typedef enum {
    D_READ,
    D_WRITE,
    D_SYNC,
    D_LWSYNC,
    D_ISYNC
  } event_dirt;

  guardt guard;
  symex_targett::sourcet source;
  event_dirt direction;
  irep_idt address;
  exprt value;
  unsigned atomic_sect_id;
  unsigned ssa_index;

  abstract_eventt() :
    direction(D_READ),
    atomic_sect_id(0),
    ssa_index(-1)
  {
    guard.make_false();
    value.make_nil();
  }

  abstract_eventt(const guardt &_guard,
      const symex_targett::sourcet &_source,
      const event_dirt _direction,
      const irep_idt &_address,
      const exprt &_value,
      const unsigned _atomic_sect_id,
      const unsigned _ssa_index) :
    guard(_guard),
    source(_source),
    direction(_direction),
    address(_address),
    value(_value),
    atomic_sect_id(_atomic_sect_id),
    ssa_index(_ssa_index)
  {
  }

  void print(std::ostream &os) const;
};

inline std::ostream& operator<<(std::ostream& os, const abstract_eventt &e)
{
  e.print(os);
  return os;
}

class abstract_events_per_processort
{
public:
  abstract_events_per_processort():
    equation(0),
    parent_event(0)
  {
  }

  void initialize(
      const symex_target_equationt &_equation,
      const abstract_events_per_processort * _parent);

  void add_abstract_event(
      const goto_symex_statet &state,
      const abstract_eventt::event_dirt direction,
      const irep_idt &address,
      const exprt &value);

  const symex_target_equationt * equation;
  abstract_eventt const* parent_event;
  typedef std::list<abstract_eventt> abstract_eventst;
  abstract_eventst abstract_events;
};

typedef std::list<abstract_events_per_processort> abstract_events_in_program_ordert;

class abstract_event_structuret
{
public:
  abstract_event_structuret(
      symex_target_equationt &_equation,
      messaget &_message):
    equation(_equation),
    memory_model("undefined"),
    message(_message),
    mm(0),
    poc(0)
  {
  }

  ~abstract_event_structuret();

  void set_memory_model(const std::string &_memory_model)
  {
    memory_model=_memory_model;
  }

  abstract_events_per_processort &add_processor(
      const abstract_events_per_processort * parent)
  {
    assert(abstract_events_in_po.empty() || parent);
    abstract_events_in_po.push_back(abstract_events_per_processort());
    abstract_events_in_po.back().initialize(equation, parent);
    return abstract_events_in_po.back();
  }

  void add_partial_order_constraints(const namespacet &ns);

  void build_concrete_event_structure(
    const prop_convt &prop_conv,
    concrete_event_structuret &ces) const;

  abstract_events_in_program_ordert::size_type n_processors() const
  {
    return abstract_events_in_po.size();
  }

protected:
  symex_target_equationt &equation;
  std::string memory_model;
  messaget &message;
  abstract_events_in_program_ordert abstract_events_in_po;
  memory_model_baset *mm;
  partial_order_concurrencyt *poc;
};

#endif
