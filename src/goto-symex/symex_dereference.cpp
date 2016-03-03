/*******************************************************************\

Module: Symbolic Execution of ANSI-C

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#include <util/expr_util.h>
#include <util/pointer_offset_size.h>
#include <util/arith_tools.h>
#include <util/base_type.h>
#include <util/byte_operators.h>

#include <pointer-analysis/value_set_dereference.h>
#include <pointer-analysis/rewrite_index.h>
#include <langapi/language_util.h>

#include <ansi-c/c_types.h>

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
  statet &state,
  guardt &guard)
{
  bool result=false;

  // Could be member, could be if, could be index.

  if(expr.id()==ID_member)
    result=dereference_rec_address_of(
      to_member_expr(expr).struct_op(), state, guard);
  else if(expr.id()==ID_if)
  {
    // the condition is not an address
    result=dereference_rec(
      to_if_expr(expr).cond(), state, guard, false);

    // add to guard?
    result=dereference_rec_address_of(
      to_if_expr(expr).true_case(), state, guard) || result;
    result=dereference_rec_address_of(
      to_if_expr(expr).false_case(), state, guard) || result;
  }
  else if(expr.id()==ID_index)
  {
    // the index is not an address
    result=dereference_rec(
      to_index_expr(expr).index(), state, guard, false);

    // the array _is_ an address
    result=dereference_rec_address_of(
      to_index_expr(expr).array(), state, guard) || result;
  }
  else
  {
    // give up and dereference
    
    result=dereference_rec(expr, state, guard, false);
  }

  return result;
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
  guardt &guard,
  const bool write)
{
  bool result=false;
  const typet type_before=expr.type();

  // use the simplifier to perform some address arithmetic
  if(expr.id()==ID_address_of)
    do_simplify(expr);

  if(expr.id()==ID_dereference)
  {
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
      to_address_of_expr(expr).object(), state, guard);
  }
  else
  {
    Forall_operands(it, expr)
      result=dereference_rec(*it, state, guard, write) || result;
  }

  if(result)
    expr.remove(ID_C_expr_simplified);

  // we really shouldn't have changed the expression type
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

  // start the recursion!
  guardt guard;  
  dereference_rec(expr, state, guard, write);
  // dereferencing may introduce new symbol_exprt
  // (like __CPROVER_memory)
  state.rename(expr, ns, goto_symex_statet::L1);
}
