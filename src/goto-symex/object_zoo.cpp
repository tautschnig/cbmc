/*******************************************************************\

Module: Manage stack/heap objects and add pointer constraints

Author: Michael Tautschnig, mt@eecs.qmul.ac.uk

\*******************************************************************/

#include <iostream>

#include <util/pointer_offset_size.h>
#include <util/byte_operators.h>
#include <util/guard.h>
#include <util/base_type.h>
#include <util/config.h>
#include <util/cprover_prefix.h>
#include <util/prefix.h>
#include <util/simplify_expr.h>

#include "object_zoo.h"

/*******************************************************************\

Function: object_zoot::object_zoot

  Inputs: 

 Outputs:

 Purpose:

\*******************************************************************/

object_zoot::object_zoot(
  const namespacet &_ns,
  symex_targett &_target):
  ns(_ns),
  target(_target),
  ptr_arith_type(unsignedbv_typet(config.ansi_c.pointer_width))

{
}

/*******************************************************************\

Function: object_zoot::add_heap_object

  Inputs: 

 Outputs:

 Purpose:

\*******************************************************************/

void object_zoot::add_heap_object(
  const symbolt &symbol,
  const symbol_exprt &symbol_expr,
  const symex_targett::sourcet &source,
  const exprt &guard)
{
  std::pair<heapt::iterator, bool> entry=
    heap.insert(std::make_pair(symbol_expr.get_identifier(), objectt()));

  if(entry.second)
  {
    std::cerr << "Heap object: " << symbol_expr.get_identifier() << std::endl;
    entry.first->second.object=symbol_expr;
    entry.first->second.size=size_of_expr(symbol.type, ns);
    entry.first->second.guard=guard;
    entry.first->second.address_taken=false;
  }
  else
    assert(false);
}

/*******************************************************************\

Function: object_zoot::add_stack_object

  Inputs: 

 Outputs:

 Purpose:

\*******************************************************************/

void object_zoot::add_stack_object(
  const symbolt &symbol,
  const symbol_exprt &symbol_expr,
  const symex_targett::sourcet &source,
  const exprt &guard)
{
  std::pair<stackt::iterator, bool> entry=
    stack.insert(std::make_pair(symbol_expr.get_identifier(), objectt()));

  if(entry.second)
  {
    std::cerr << "Stack object: " << symbol_expr.get_identifier() << std::endl;
    entry.first->second.object=symbol_expr;
    entry.first->second.size=size_of_expr(symbol.type, ns);
    entry.first->second.guard=guard;
    entry.first->second.address_taken=false;
  }
  else
    assert(false);
}

/*******************************************************************\

Function: object_zoot::record_static_symbols

  Inputs: 

 Outputs:

 Purpose:

\*******************************************************************/

void object_zoot::record_static_objects(
  const symex_targett::sourcet &source)
{
  const symbol_tablet &symbol_table=ns.get_symbol_table();

  // based on linking/entry_point.cpp static_lifetime_init
  forall_symbols(it, symbol_table.symbols)
  {
    const symbolt &symbol=it->second;

    if(!symbol.is_static_lifetime) continue;

    if(symbol.is_type) continue;

    const irep_idt &identifier=it->first;

    // special values
    if(identifier==CPROVER_PREFIX "constant_infinity_uint" ||
       identifier==CPROVER_PREFIX "memory" ||
       identifier=="c::__func__" ||
       identifier=="c::__FUNCTION__" ||
       identifier=="c::__PRETTY_FUNCTION__" ||
       identifier=="c::argc'" ||
       identifier=="c::argv'" ||
       identifier=="c::envp'" ||
       identifier=="c::envp_size'")
      continue;

    // just for linking
    if(has_prefix(id2string(identifier), CPROVER_PREFIX "architecture_"))
      continue;

    // check type
    if(symbol.type.id()==ID_code)
      continue;

    // for now we'll assume that static objects are allocated in the
    // same memory region as the heap
    if(symbol.is_thread_local)
    {
      ssa_exprt ssa_l0(symbol.symbol_expr());
      ssa_l0.set_level_0(0);

      record_static_thread_local(ssa_l0, source, guardt());
    }
    else
      add_heap_object(
        symbol,
        symbol.symbol_expr(),
        source,
        true_exprt());
  }
}

/*******************************************************************\

Function: object_zoot::record_static_thread_local

  Inputs: 

 Outputs:

 Purpose:

\*******************************************************************/

void object_zoot::record_static_thread_local(
  const ssa_exprt &ssa_l0,
  const symex_targett::sourcet &source,
  const guardt &guard)
{
  const irep_idt identifier=ssa_l0.get_object_name();

  const symbolt &symbol=ns.lookup(identifier);
  std::cerr << "Thread-local symbol: " << identifier << std::endl;

  add_heap_object(symbol, ssa_l0, source, guard.as_expr());
}

/*******************************************************************\

Function: object_zoot::record_decl

  Inputs: 

 Outputs:

 Purpose:

\*******************************************************************/

void object_zoot::record_decl(
  const ssa_exprt &ssa_l1,
  const symex_targett::sourcet &source,
  const guardt &guard)
{
  const irep_idt identifier=ssa_l1.get_object_name();

  const symbolt &symbol=ns.lookup(identifier);
  std::cerr << "Stack symbol: " << identifier << std::endl;

  add_stack_object(symbol, ssa_l1, source, guard.as_expr());
}

/*******************************************************************\

Function: object_zoot::record_dead

  Inputs: 

 Outputs:

 Purpose:

\*******************************************************************/

void object_zoot::record_dead(
  const ssa_exprt &ssa_l1,
  const symex_targett::sourcet &source,
  const guardt &guard)
{
  const irep_idt identifier=ssa_l1.get_identifier();

  std::cerr << "Dead: " << identifier << std::endl;
  stackt::iterator entry=stack.find(identifier);
  assert(entry!=stack.end());

  objectt &o=entry->second;

  o.guard=and_exprt(o.guard, not_exprt(guard.as_expr()));
  simplify(o.guard, ns);

  if(o.guard.is_false())
    stack.erase(entry);
}

/*******************************************************************\

Function: object_zoot::record_malloc

  Inputs: 

 Outputs:

 Purpose:

\*******************************************************************/

void object_zoot::record_malloc(
  const ssa_exprt &ssa_l1,
  const symex_targett::sourcet &source,
  const guardt &guard)
{
  const irep_idt identifier=ssa_l1.get_object_name();

  const symbolt &symbol=ns.lookup(identifier);

  add_heap_object(symbol, ssa_l1, source, guard.as_expr());
}

/*******************************************************************\

Function: object_zoot::get_address_taken

  Inputs: 

 Outputs:

 Purpose:

\*******************************************************************/

void object_zoot::get_address_taken(const exprt &expr)
{
  if(expr.id()!=ID_address_of)
  {
    forall_operands(it, expr)
      get_address_taken(*it);

    return;
  }

  exprt object=to_address_of_expr(expr).object();

  if(object.id()==ID_index)
  {
    const index_exprt &idx=to_index_expr(object);
    assert(idx.index().is_zero());

    object=idx.array();
  }

  // TODO ignore constants for now
  if(object.id()==ID_string_constant ||
     object.id()==ID_array ||
     object.id()==ID_label)
    return;

  std::cerr << object.pretty() << std::endl;
  assert(object.id()==ID_symbol);
  const symbol_exprt &symbol_expr=to_symbol_expr(object);

  const irep_idt identifier=
    symbol_expr.get_bool(ID_C_SSA_symbol)?
    to_ssa_expr(symbol_expr).get_object_name():
    to_symbol_expr(symbol_expr).get_identifier();

  const symbolt &symbol=ns.lookup(identifier);

  // no constraints and no objects for functions
  if(ns.follow(symbol.type).id()==ID_code)
    return;

  const irep_idt &l1_identifier=
    to_symbol_expr(symbol_expr).get_identifier();

  std::cerr << object.pretty() << std::endl;
  if(symbol.is_static_lifetime ||
     symbol.type.get_bool("#dynamic"))
  {
    heapt::iterator entry=heap.find(l1_identifier);
    assert(entry!=heap.end());

    entry->second.address_taken=true;
  }
  else
  {
    std::cerr << "identifier=" << l1_identifier << std::endl;
    stackt::iterator entry=stack.find(l1_identifier);
    assert(entry!=stack.end());

    entry->second.address_taken=true;
  }
}

/*******************************************************************\

Function: object_zoot::deref_one

  Inputs: 

 Outputs:

 Purpose:

\*******************************************************************/

void object_zoot::deref_one(
  const exprt &pointer,
  const exprt &guard,
  const typet &type,
  const objectt &obj_entry,
  exprt &result) const
{
  if(!obj_entry.address_taken)
    return;

  const exprt &object=obj_entry.object;
  address_of_exprt obj_address(object);

  implies_exprt obj_exists(guard, obj_entry.guard);

  const exprt &obj_size=obj_entry.size;

  binary_relation_exprt obj_lower(pointer, ID_ge, obj_address);
  binary_relation_exprt obj_upper(pointer, ID_lt,
                                  plus_exprt(obj_address, obj_size));

  and_exprt cond(obj_exists, obj_lower, obj_upper);

  byte_extract_exprt be(byte_extract_id(),
                        object,
                        minus_exprt(
                          typecast_exprt(pointer, ptr_arith_type),
                          typecast_exprt(obj_address, ptr_arith_type)),
                        type);

  result=if_exprt(cond, be, result);

  if(base_type_eq(object.type(), type, ns))
    result=if_exprt(and_exprt(obj_exists, equal_exprt(pointer, obj_address)),
                    object,
                    result);
}

/*******************************************************************\

Function: object_zoot::dereference

  Inputs: 

 Outputs:

 Purpose:

\*******************************************************************/

exprt object_zoot::dereference(
  const dereference_exprt &expr,
  const guardt &guard) const
{
  const exprt &pointer=expr.pointer();
  exprt guard_expr=guard.as_expr();

  exprt result=side_effect_expr_nondett(expr.type());

  for(heapt::const_iterator
      it=heap.begin();
      it!=heap.end();
      ++it)
    deref_one(pointer, guard_expr, expr.type(), it->second, result);

  for(stackt::const_iterator
      it=stack.begin();
      it!=stack.end();
      ++it)
    deref_one(pointer, guard_expr, expr.type(), it->second, result);

  return result;
}

