/*******************************************************************\

Module: Manage stack/heap objects and add pointer constraints

Author: Michael Tautschnig, mt@eecs.qmul.ac.uk

\*******************************************************************/

#ifndef CPROVER_OBJECT_ZOO_H
#define CPROVER_OBJECT_ZOO_H

#include "symex_target.h"

class dereference_exprt;
class guardt;
class namespacet;
class ssa_exprt;

class object_zoot
{
public:
  object_zoot(const namespacet &_ns, symex_targett &_target);

  // layer 1: gather objects
  // eventually these should also add constraints of on disjoint-ness
  // (hello separation logic!)
  void record_static_objects(const symex_targett::sourcet &source);
  void record_static_thread_local(
    const ssa_exprt &ssa_l0,
    const symex_targett::sourcet &source,
    const guardt &guard);
  void record_decl(
    const ssa_exprt &ssa_l1,
    const symex_targett::sourcet &source,
    const guardt &guard);
  void record_dead(
    const ssa_exprt &ssa_l1,
    const symex_targett::sourcet &source,
    const guardt &guard);
  void record_malloc(
    const ssa_exprt &ssa_l1,
    const symex_targett::sourcet &source,
    const guardt &guard);

  // layer 2: refine points-to set
  // this is unsound for uninitialized pointers
  void get_address_taken(const exprt &expr);

  // layer 3: enumerate candidate objects in dereferencing
  exprt dereference(
    const dereference_exprt &expr,
    const guardt &guard) const;

protected:
  const namespacet &ns;
  symex_targett &target;
  const typet ptr_arith_type;

  struct objectt
  {
    exprt object;
    exprt size;
    exprt guard;
    bool address_taken;
  };

  typedef hash_map_cont<irep_idt, objectt, irep_id_hash> heapt;
  heapt heap;

  typedef hash_map_cont<irep_idt, objectt, irep_id_hash> stackt;
  stackt stack;

  void add_heap_object(
    const symbolt &symbol,
    const symbol_exprt &symbol_expr,
    const symex_targett::sourcet &source,
    const exprt &guard);

  void add_stack_object(
    const symbolt &symbol,
    const symbol_exprt &symbol_expr,
    const symex_targett::sourcet &source,
    const exprt &guard);

  void deref_one(
    const exprt &pointer,
    const exprt &guard,
    const typet &type,
    const objectt &obj_entry,
    exprt &result) const;
};

#endif
