/*******************************************************************\

Module: Symbolic Execution of ANSI-C

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#include <iostream>

#include <util/simplify_expr.h>
#include <util/expr_util.h>
#include <util/pointer_offset_size.h>
#include <util/arith_tools.h>
#include <util/base_type.h>
#include <util/byte_operators.h>
#include <util/i2string.h>
#include <util/rename.h>

#include <ansi-c/c_types.h>

#include <pointer-analysis/dereference.h>

#include "goto_symex.h"
#include "symex_dereference_state.h"

/*******************************************************************\

Function: goto_symext::dereference_rec_address_of

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool goto_symext::dereference_rec_address_of(
  exprt &expr,
  statet &state)
{
  bool result=false;

  // Could be member, could be if, could be index.

  if(expr.id()==ID_member)
    result=dereference_rec_address_of(
      to_member_expr(expr).struct_op(), state);
  else if(expr.id()==ID_if)
  {
    // the condition is not an address
    result=dereference_rec(
      to_if_expr(expr).cond(), state, false);

    // add to guard?
    result=dereference_rec_address_of(
      to_if_expr(expr).true_case(), state) || result;
    result=dereference_rec_address_of(
      to_if_expr(expr).false_case(), state) || result;
  }
  else if(expr.id()==ID_index)
  {
    // the index is not an address
    result=dereference_rec(
      to_index_expr(expr).index(), state, false);

    // the array _is_ an address
    result=dereference_rec_address_of(
      to_index_expr(expr).array(), state) || result;
  }
  else
  {
    // give up and dereference
    
    result=dereference_rec(expr, state, false);
  }

  return result;
}

/*******************************************************************\

Function: dereferencet::make_invalid_object

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_symext::make_invalid_object(
  dereference_callbackt &dereference_callback,
  symbol_tablet &new_symbol_table,
  exprt &pointer,
  const typet &type)
{
  assert(type.id()!=ID_empty);

  // expr has pointer type, but constant propagation didn't yield
  // any useful values; it would be nice to replace this by nondet
  // symbols, but assignments to these aren't handled

  const symbolt *failed_symbol;
  symbol_exprt failure_value;

  if(dereference_callback.has_failed_symbol(
      pointer, failed_symbol))
  {
    // yes!
    failure_value=failed_symbol->symbol_expr();
    failure_value.set(ID_C_invalid_object, true);
  }
  else
  {
    // else: produce new symbol

    symbolt symbol;
    symbol.name="symex::invalid_object"+i2string(nondet_count++);
    symbol.base_name="invalid_object";
    symbol.type=type;
    symbol.is_thread_local=true;
    symbol.is_static_lifetime=false;
    symbol.is_file_local=true;

    // make it a lvalue, so we can assign to it
    symbol.is_lvalue=true;

    get_new_name(symbol, ns);

    failure_value=symbol.symbol_expr();
    failure_value.set(ID_C_invalid_object, true);

    new_symbol_table.move(symbol);
  }

  pointer=ssa_exprt(failure_value);
}

/*******************************************************************\

Function: goto_symext::dereference_rec

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool goto_symext::dereference_rec(
  exprt &expr,
  statet &state,
  const bool write)
{
  bool result=false;
  const typet type_before=expr.type();

  // use the simplifier to perform some address arithmetic
  if(expr.id()==ID_address_of)
    do_simplify(expr);

  if(expr.id()==ID_dereference)
  {
#if 0
    if(expr.operands().size()!=1)
      throw "dereference takes one operand";

    exprt tmp1;
    tmp1.swap(expr.op0());
    
    // first make sure there are no dereferences in there
    dereference_rec(tmp1, state, guard, false);

    // we need to set up some elaborate call-backs
    symex_dereference_statet symex_dereference_state(*this, state);

    value_set_dereferencet dereference(
      ns,
      new_symbol_table,
      options,
      symex_dereference_state);      
    
    // std::cout << "**** " << from_expr(ns, "", tmp1) << std::endl;
    exprt tmp2=dereference.dereference(
      tmp1, guard, write?value_set_dereferencet::WRITE:value_set_dereferencet::READ);
    //std::cout << "**** " << from_expr(ns, "", tmp2) << std::endl;

    expr.swap(tmp2);
    
    // this may yield a new auto-object
    trigger_auto_object(expr, state);
#else
    // Compute *expr, resulting in (partial) renaming down to L2
    expr=to_dereference_expr(expr).pointer();

    // first make sure there are no dereferences in there
    dereference_rec(expr, state, false);

    // now rename, enables propagation; we record reads of shared
    // pointers right here
    state.rename(expr, ns);

    // now try simplifier on it
    do_simplify(expr);

    // expand expression
#if 1
    std::cerr << "To expand: " << from_expr(ns, "", expr) << std::endl;
    std::string exp=from_expr(ns, "", expr);
    expr=expand_deref(expr, false);
    std::cerr << "done: " << from_expr(ns, "", simplify_expr(expr, ns)) << std::endl;
    assert(exp!="next!0@4#2");
#else
    expr=expand_deref(expr, true);
#endif

    // actually do the dereferencing
    exprt is_invalid;
    expr=::dereference(expr, ns, is_invalid);
#if 1
    std::cerr << "deref'd: " << from_expr(ns, "", expr) << std::endl;
#endif

    // beautify the result, in particular getting rid of some
    // byte_extract expressions
    do_simplify(expr);

    do_simplify(is_invalid);
    if(!is_invalid.is_false())
    {
      exprt invalid;
      symex_dereference_statet symex_dereference_state(*this, state);
      make_invalid_object(
        symex_dereference_state,
        new_symbol_table,
        invalid,
        type_before);

      if(is_invalid.is_true())
        expr=invalid;
      else
        expr=if_exprt(is_invalid, invalid, expr);
    }
#if 1
    std::cerr << "final: " << from_expr(ns, "", expr) << std::endl;
#endif
#endif

    result=true;
  }
  else if(expr.id()==ID_address_of &&
          to_address_of_expr(expr).object().id()==ID_dereference)
  {
    // ANSI-C guarantees &*p == p no matter what p is,
    // even if it's complete garbage
    expr=to_dereference_expr(
      to_address_of_expr(expr).object()).pointer();

    result=true;
  }
  else if(expr.id()==ID_address_of)
  {
    result=dereference_rec_address_of(
      to_address_of_expr(expr).object(), state);
  }
  else
  {
    Forall_operands(it, expr)
      result=dereference_rec(*it, state, write) || result;
  }

  if(result)
    expr.remove(ID_C_expr_simplified);

  // we really shouldn't have changed the expression type
  if(!base_type_eq(expr.type(), type_before, ns))
  {
    std::cerr << "expr.type()=" << from_type(ns, "", expr.type()) << std::endl;
    std::cerr << "type_before=" << from_type(ns, "", type_before) << std::endl;
    std::cerr << "expr=" << expr.pretty() << std::endl;
  }
  assert(base_type_eq(expr.type(), type_before, ns));

  return result;
}

/*******************************************************************\

Function: goto_symext::dereference

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_symext::dereference(
  exprt &expr,
  statet &state,
  const bool write)
{
  // The expression needs to be renamed to level 1
  // in order to distinguish addresses of local variables
  // from different frames. Would be enough to rename
  // symbols whose address is taken.
  assert(!state.call_stack().empty());
  state.rename(expr, ns, goto_symex_statet::L1);
  // really, we need to use another dereferencing implementation,
  // such as dereferencet, with L2 renaming on demand

  // start the recursion!
#if 0
  guardt guard;
  dereference_rec(expr, state, guard, write);
  // dereferencing may introduce new symbol_exprt
  // (like __CPROVER_memory)
  state.rename(expr, ns, goto_symex_statet::L1);
#else
  std::cerr << "Before deref: " << from_expr(ns, "", expr) << std::endl;
  dereference_rec(expr, state, write);
#endif
}
