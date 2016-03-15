/*******************************************************************\

Module:

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#include <cassert>

#include "simplify_expr_class.h"
#include "expr.h"
#include "namespace.h"
#include "std_expr.h"
#include "pointer_offset_size.h"
#include "arith_tools.h"
#include "config.h"
#include "expr_util.h"
#include "threeval.h"
#include "prefix.h"
#include "pointer_predicates.h"
#include "byte_operators.h"
#include "base_type.h"

/*******************************************************************\

Function: simplify_exprt::simplify_address_of

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool simplify_exprt::simplify_address_of(address_of_exprt &expr)
{
  const typet &expr_type=ns.follow(expr.type());
  exprt &object=expr.object();

  if(expr_type.id()!=ID_pointer) return true;

  if(object.id()==ID_index ||
     object.id()==ID_member)
  {
    object_descriptor_exprt ode;
    ode.build(object, ns);

    byte_extract_exprt be(byte_extract_id());
    be.type()=object.type();
    be.op()=ode.root_object();
    be.offset()=ode.offset();

    address_of_exprt tmp(be);
    simplify_address_of(tmp);
    expr.swap(tmp);

    return false;
  }
  else if(object.id()==ID_byte_extract_little_endian ||
          object.id()==ID_byte_extract_big_endian)
  {
    // address_of(byte_extract(op, offset, t)) is
    // address_of(op) + offset with adjustments for arrays

    byte_extract_exprt &be=to_byte_extract_expr(object);
    simplify_rec(be.op());

    exprt result=address_of_exprt(be.op());
    result.type()=expr.type();

    simplify_address_of(to_address_of_expr(result));
    simplify_rec(be.offset());

    // do (expr.type() *)(((char *)op)+offset)
    if(!be.offset().is_zero())
    {
      unsignedbv_typet byte_type(8);
      result.make_typecast(pointer_typet(byte_type));
      simplify_node(result);

      result=plus_exprt(result, be.offset());
      simplify_node(result);
    }

    result.make_typecast(expr.type());
    simplify_node(result);
    expr.swap(result);

    return false;
  }

  simplify_rec(object);

  if(object.id()==ID_if)
  {
    if_exprt if_expr=lift_if(expr, 0);
    simplify_address_of(to_address_of_expr(if_expr.true_case()));
    simplify_address_of(to_address_of_expr(if_expr.false_case()));
    simplify_if(if_expr);
    expr.swap(if_expr);

    return false;
  }

#if 0
  pointer_arithmetict ptr(expr);
  assert(!ptr.pointer.is_nil());
  if(!ptr.offset.is_nil())
  {
    simplify_node(ptr.offset);
    plus_exprt plus(ptr.pointer, ptr.offset);
    if(expr!=plus)
    {
      expr.swap(plus);
      simplify_rec(expr);
      return false;
    }
  }
#endif
  
  if(object.id()==ID_dereference)
  {
    // simplify &*p to p
    exprt tmp=to_dereference_expr(object).pointer();
    expr.swap(tmp);
    return false;
  }
  else if(object.id()!=ID_symbol &&
          object.id()!=ID_string_constant &&
          object.id()!=ID_label &&
          object.id()!=ID_array &&
          object.id()!=ID_compound_literal)
    throw "simplify_address_of does not handle "+object.id_string();

  if((ns.follow(object.type()).id()==ID_array ||
      ns.follow(object.type()).id()==ID_struct) &&
     !base_type_eq(expr_type.subtype(), object.type(), ns))
  {
    unsignedbv_typet size_type(config.ansi_c.pointer_width);

    // turn &a of type T[i][j] into &(a[0][0])
    // turn &s of type struct T { TT i; } into &(s.i)
    for(const typet *t=&(ns.follow(object.type()));
        (t->id()==ID_array || t->id()==ID_struct) &&
        !base_type_eq(expr_type.subtype(), *t, ns);
        ) // no successor computation
    {
      if(t->id()==ID_array)
      {
        object=index_exprt(object, gen_zero(size_type));
        t=&(ns.follow(t->subtype()));
      }
      else
      {
        const struct_typet &s_t=to_struct_type(*t);
        const struct_typet::componentst &components=
          s_t.components();
        assert(!components.empty());
        const struct_typet::componentt &c=components.front();

        object=member_exprt(object, c.get_name(), c.type());
        t=&(ns.follow(c.type()));
      }
    }

    // struct fields might have different types
    if(!base_type_eq(object.type(), expr_type.subtype(), ns))
    {
      expr.make_typecast(expr_type);
      to_typecast_expr(expr).op().type().subtype()=object.type();
    }

    return false;
  }

  return true;
}

/*******************************************************************\

Function: simplify_exprt::simplify_pointer_offset

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool simplify_exprt::simplify_pointer_offset(exprt &expr)
{
  if(expr.operands().size()!=1) return true;

  exprt &ptr=expr.op0();

  if(ptr.id()==ID_if && ptr.operands().size()==3)
  {
    if_exprt if_expr=lift_if(expr, 0);
    simplify_pointer_offset(if_expr.true_case());
    simplify_pointer_offset(if_expr.false_case());
    simplify_if(if_expr);
    expr.swap(if_expr);

    return false;
  }

  if(ptr.type().id()!=ID_pointer) return true;
  
  if(ptr.id()==ID_address_of)
  {
    if(ptr.operands().size()!=1) return true;

    mp_integer offset=compute_pointer_offset(ptr.op0(), ns);

    if(offset!=-1)
    {
      expr=from_integer(offset, expr.type());
      return false;
    }    
  }
  else if(ptr.id()==ID_typecast) // pointer typecast
  {
    if(ptr.operands().size()!=1) return true;
    
    const typet &op_type=ns.follow(ptr.op0().type());
    
    if(op_type.id()==ID_pointer)
    {
      // Cast from pointer to pointer.
      // This just passes through, remove typecast.
      exprt tmp=ptr.op0();
      ptr=tmp;
    
      // recursive call
      simplify_node(expr);
      return false;
    }
    else if(op_type.id()==ID_signedbv ||
            op_type.id()==ID_unsignedbv)
    {
      // Cast from integer to pointer, say (int *)x.
      
      if(ptr.op0().is_constant())
      {
        // (T *)0x1234 -> 0x1234
        exprt tmp=ptr.op0();
        tmp.make_typecast(expr.type());
        simplify_node(tmp);
        expr.swap(tmp);
        return false;
      }
      else
      {
        // We do a bit of special treatment for (TYPE *)(a+(int)&o),
        // which is re-written to 'a'.

        typet type=ns.follow(expr.type());      
        exprt tmp=ptr.op0();
        if(tmp.id()==ID_plus && tmp.operands().size()==2)
        {
          if(tmp.op0().id()==ID_typecast &&
             tmp.op0().operands().size()==1 &&
             tmp.op0().op0().id()==ID_address_of)
          {
            expr=tmp.op1();
            if(type!=expr.type())
              expr.make_typecast(type);

            simplify_node(expr);
            return false;
          }
          else if(tmp.op1().id()==ID_typecast &&
                  tmp.op1().operands().size()==1 &&
                  tmp.op1().op0().id()==ID_address_of)
          {
            expr=tmp.op0();
            if(type!=expr.type())
              expr.make_typecast(type);

            simplify_node(expr);
            return false;
          }
        }
      }
    }
  }
  else if(ptr.id()==ID_plus) // pointer arithmetic
  {
    exprt::operandst ptr_expr;
    exprt::operandst int_expr;
    
    forall_operands(it, ptr)
    {
      if(it->type().id()==ID_pointer)
        ptr_expr.push_back(*it);
      else if(!it->is_zero())
      {
        exprt tmp=*it;
        if(tmp.type()!=expr.type())
        {
          tmp.make_typecast(expr.type());
          simplify_node(tmp);
        }
        
        int_expr.push_back(tmp);
      }
    }

    if(ptr_expr.size()!=1 || int_expr.empty())
      return true;
      
    typet pointer_type=ptr_expr.front().type();

    mp_integer element_size=
      pointer_offset_size(pointer_type.subtype(), ns);
    
    if(element_size==0) return true;
    
    // this might change the type of the pointer!
    exprt ptr_off(ID_pointer_offset, expr.type());
    ptr_off.copy_to_operands(ptr_expr.front());
    simplify_node(ptr_off);

    exprt sum;
    
    if(int_expr.size()==1)
      sum=int_expr.front();
    else
    {
      sum=exprt(ID_plus, expr.type());
      sum.operands()=int_expr;
    }
      
    simplify_node(sum);
    
    exprt size_expr=
      from_integer(element_size, expr.type());
        
    binary_exprt product(sum, ID_mult, size_expr, expr.type());
  
    simplify_node(product);
    
    expr=binary_exprt(ptr_off, ID_plus, product, expr.type());

    simplify_node(expr);
    
    return false;
  }
  else if(ptr.id()==ID_constant &&
          ptr.get(ID_value)==ID_NULL)
  {
    expr=gen_zero(expr.type());

    simplify_node(expr);

    return false;
  }

  return true;
}

/*******************************************************************\

Function: simplify_exprt::simplify_inequality_address_of

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool simplify_exprt::simplify_inequality_address_of(exprt &expr)
{
  assert(expr.type().id()==ID_bool);
  assert(expr.operands().size()==2);
  assert(expr.id()==ID_equal || expr.id()==ID_notequal);

  exprt tmp0=expr.op0();
  if(tmp0.id()==ID_typecast)
    tmp0=expr.op0().op0();
  if(tmp0.op0().id()==ID_index &&
     to_index_expr(tmp0.op0()).index().is_zero())
    tmp0=address_of_exprt(to_index_expr(tmp0.op0()).array());
  exprt tmp1=expr.op1();
  if(tmp1.id()==ID_typecast)
    tmp1=expr.op1().op0();
  if(tmp1.op0().id()==ID_index &&
     to_index_expr(tmp1.op0()).index().is_zero())
    tmp1=address_of_exprt(to_index_expr(tmp1.op0()).array());
  assert(tmp0.id()==ID_address_of);
  assert(tmp1.id()==ID_address_of);

  if(tmp0.operands().size()!=1) return true;
  if(tmp1.operands().size()!=1) return true;
  
  if(tmp0.op0().id()==ID_symbol &&
     tmp1.op0().id()==ID_symbol)
  {
    bool equal=
       tmp0.op0().get(ID_identifier)==
       tmp1.op0().get(ID_identifier);
       
    expr.make_bool(expr.id()==ID_equal?equal:!equal);
    
    return false;
  }
  
  return true;
}

/*******************************************************************\

Function: simplify_exprt::simplify_inequality_pointer_object

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool simplify_exprt::simplify_inequality_pointer_object(exprt &expr)
{
  assert(expr.type().id()==ID_bool);
  assert(expr.operands().size()==2);
  assert(expr.id()==ID_equal || expr.id()==ID_notequal);

  forall_operands(it, expr)
  {
    assert(it->id()==ID_pointer_object);
    assert(it->operands().size()==1);
    const exprt &op=it->op0();

    if(op.id()==ID_address_of)
    {
      if(op.operands().size()!=1 ||
         (op.op0().id()!=ID_symbol &&
          op.op0().id()!=ID_dynamic_object &&
          op.op0().id()!=ID_string_constant))
        return true;
    }
    else if(op.id()!=ID_constant ||
            op.get(ID_value)!=ID_NULL)
      return true;
  }

  bool equal=expr.op0().op0()==expr.op1().op0();

  expr.make_bool(expr.id()==ID_equal?equal:!equal);

  return false;
}

/*******************************************************************\

Function: simplify_exprt::simplify_pointer_object

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool simplify_exprt::simplify_pointer_object(exprt &expr)
{
  if(expr.operands().size()!=1) return true;
  
  exprt &op=expr.op0();
  
  bool result=simplify_object(op);

  if(op.id()==ID_if)
  {
    const if_exprt &if_expr=to_if_expr(op);
    exprt cond=if_expr.cond();

    exprt p_o_false=expr;
    p_o_false.op0()=if_expr.false_case();

    expr.op0()=if_expr.true_case();

    expr=if_exprt(cond, expr, p_o_false, expr.type());
    simplify_rec(expr);

    return false;
  }

  return result;
}

/*******************************************************************\

Function: simplify_exprt::simplify_dynamic_object

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool simplify_exprt::simplify_dynamic_object(exprt &expr)
{
  if(expr.operands().size()!=1) return true;
  
  exprt &op=expr.op0();

  if(op.id()==ID_if && op.operands().size()==3)
  {
    if_exprt if_expr=lift_if(expr, 0);
    simplify_dynamic_object(if_expr.true_case());
    simplify_dynamic_object(if_expr.false_case());
    simplify_if(if_expr);
    expr.swap(if_expr);

    return false;
  }
  
  bool result=true;
  
  if(!simplify_object(op)) result=false;

  // NULL is not dynamic
  if(op.id()==ID_constant && op.get(ID_value)==ID_NULL)
  {
    expr=false_exprt();
    return false;
  }  

  // &something depends on the something
  if(op.id()==ID_address_of && op.operands().size()==1)
  {
    if(op.op0().id()==ID_symbol)
    {
      const irep_idt identifier=to_symbol_expr(op.op0()).get_identifier();

      // this is for the benefit of symex
      expr.make_bool(has_prefix(id2string(identifier), "symex_dynamic::"));
      return false;
    }
    else if(op.op0().id()==ID_string_constant)
    {
      expr=false_exprt();
      return false;
    }
    else if(op.op0().id()==ID_array)
    {
      expr=false_exprt();
      return false;
    }
  }
  
  return result;
}

/*******************************************************************\

Function: simplify_exprt::simplify_invalid_pointer

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool simplify_exprt::simplify_invalid_pointer(exprt &expr)
{
  if(expr.operands().size()!=1) return true;
  
  exprt &op=expr.op0();
  
  bool result=true;
  
  if(!simplify_object(op)) result=false;

  // NULL is not invalid
  if(op.id()==ID_constant && op.get(ID_value)==ID_NULL)
  {
    expr=false_exprt();
    return false;
  }  
  
  // &anything is not invalid
  if(op.id()==ID_address_of)
  {
    expr=false_exprt();
    return false;
  }  
  
  return result;
}

/*******************************************************************\

Function: simplify_exprt::objects_equal

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

tvt simplify_exprt::objects_equal(const exprt &a, const exprt &b)
{
  if(a==b) return tvt(true);

  if(a.id()==ID_address_of && b.id()==ID_address_of &&
     a.operands().size()==1 && b.operands().size()==1)
    return objects_equal_address_of(a.op0(), b.op0());

  if(a.id()==ID_constant && b.id()==ID_constant &&
     a.get(ID_value)==ID_NULL && b.get(ID_value)==ID_NULL)
    return tvt(true);

  if(a.id()==ID_constant && b.id()==ID_address_of &&
     a.get(ID_value)==ID_NULL)
    return tvt(false);

  if(b.id()==ID_constant && a.id()==ID_address_of &&
     b.get(ID_value)==ID_NULL)
    return tvt(false);

  return tvt::unknown();
}

/*******************************************************************\

Function: simplify_exprt::objects_equal_address_of

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

tvt simplify_exprt::objects_equal_address_of(const exprt &a, const exprt &b)
{
  if(a==b) return tvt(true);

  if(a.id()==ID_symbol && b.id()==ID_symbol)
  {
    if(a.get(ID_identifier)==b.get(ID_identifier))
      return tvt(true);
  }
  else if(a.id()==ID_index && b.id()==ID_index)
  {
    if(a.operands().size()==2 && b.operands().size()==2)
      return objects_equal_address_of(a.op0(), b.op0());
  }
  else if(a.id()==ID_member && b.id()==ID_member)
  {
    if(a.operands().size()==1 && b.operands().size()==1)
      return objects_equal_address_of(a.op0(), b.op0());
  }

  return tvt::unknown();
}

/*******************************************************************\

Function: simplify_exprt::simplify_object_size

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool simplify_exprt::simplify_object_size(exprt &expr)
{
  if(expr.operands().size()!=1) return true;
  
  exprt &op=expr.op0();
  
  bool result=true;
  
  if(!simplify_object(op)) result=false;
  
  if(op.id()==ID_address_of && op.operands().size()==1)
  {
    if(op.op0().id()==ID_symbol)
    {
      // just get the type
      const typet &type=ns.follow(op.op0().type());

      exprt size=size_of_expr(type, ns);

      if(size.is_not_nil())
      {
        typet type=expr.type();

        if(size.type()!=type)
        {
          size.make_typecast(type);
          simplify_node(size);
        }

        expr=size;
        return false;
      }
    }
    else if(op.op0().id()==ID_string_constant)
    {
      typet type=expr.type();
      expr=from_integer(op.op0().get(ID_value).size()+1, type);
      return false;
    }
  }
  
  return result;
}

/*******************************************************************\

Function: simplify_exprt::simplify_good_pointer

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool simplify_exprt::simplify_good_pointer(exprt &expr)
{
  if(expr.operands().size()!=1) return true;
  
  // we expand the definition
  exprt def=good_pointer_def(expr.op0(), ns);

  // recursive call
  simplify_node(def);  
  
  expr.swap(def);

  return false;
}

