/*******************************************************************\

Module: Points-to analysis using reaching-definitions information

Author: Michael Tautschnig

Date: May 2014

\*******************************************************************/

#include <util/simplify_expr.h>
#include <util/pointer_offset_size.h>
#include <util/byte_operators.h>
#include <util/arith_tools.h>

#include <ansi-c/c_types.h>

#include <analyses/reaching_definitions.h>

#include "rd_dereference.h"

/*******************************************************************\

Function: rd_dereferencet::~rd_dereferencet

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

rd_dereferencet::~rd_dereferencet()
{
}

/*******************************************************************\

Function: rd_dereferencet::operator()

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

exprt rd_dereferencet::operator()(
  const exprt &pointer,
  goto_programt::const_targett _target)
{
  target=_target;
  rec_set.clear();
  return dereferencet::operator()(pointer);
}

/*******************************************************************\

Function: rd_dereferencet::resolve

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

exprt rd_dereferencet::resolve(const symbol_exprt &symbol_expr)
{
  const irep_idt &identifier=symbol_expr.get_identifier();

  if(!rec_set[identifier].insert(target).second)
    return nil_exprt();

  const rd_range_domaint::ranges_at_loct &ranges=
    reaching_definitions[target].get(identifier);

  typedef std::set<goto_programt::const_targett> defst;
  defst defs;

  for(rd_range_domaint::ranges_at_loct::const_iterator
      it=ranges.begin();
      it!=ranges.end();
      ++it)
    defs.insert(it->first);

  exprt result=nil_exprt();

  for(defst::const_iterator it=defs.begin();
      it!=defs.end();
      ++it)
  {
    goto_programt::const_targett prev_target=target;
    target=*it;

    bool found=false;
    exprt pointer_val;

    if(target->is_assign())
    {
      found=true;
      pointer_val=to_code_assign(target->code).rhs();
    }
    else if(target->is_function_call())
    {
      const code_function_callt &function_call=
        to_code_function_call(target->code);

      if(ns.lookup(symbol_expr.get_identifier()).is_parameter)
      {
        const code_typet &code_type=
          to_code_type(ns.lookup(prev_target->function).type);

        code_function_callt::argumentst::const_iterator a_it=
          function_call.arguments().begin();
        for(code_typet::parameterst::const_iterator
            p_it=code_type.parameters().begin();
            p_it!=code_type.parameters().end() &&
            a_it!=function_call.arguments().end();
            ++p_it, ++a_it)
          if(p_it->get_identifier()==symbol_expr.get_identifier())
          {
            found=true;
            pointer_val=*a_it;
            break;
          }
      }
      else if(function_call.function().id()==ID_symbol)
      {
        // handle return value by looking up all return statements in
        // called function
        const symbol_exprt &f_sym=
          to_symbol_expr(function_call.function());
        goto_functionst::function_mapt::const_iterator f_entry=
          goto_functions.function_map.find(f_sym.get_identifier());
        assert(f_entry!=goto_functions.function_map.end());

        // TODO handle multiple returns
        forall_goto_program_instructions(t_it, f_entry->second.body)
          if(t_it->is_return() &&
             to_code_return(t_it->code).has_return_value())
          {
            found=true;
            pointer_val=to_code_return(t_it->code).return_value();
            target=t_it;
          }
      }
    }
    else
      assert(false);

    if(!found)
    {
      target=prev_target;
      continue;
    }

    target=prev_target;

    if(result.is_nil())
      result=pointer_val;
    else if(pointer_val.is_not_nil())
      result=if_exprt(
        side_effect_expr_nondett(bool_typet()),
        pointer_val,
        result);
  }

  return result;
}

/*******************************************************************\

Function: rd_dereferencet::resolve_rec

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

exprt rd_dereferencet::resolve_rec(const exprt &expr)
{
  if(expr.id()==ID_dereference)
  {
    const dereference_exprt &deref=to_dereference_expr(expr);

    return dereferencet::operator()(deref.pointer());
  }
  else if(expr.id()==ID_symbol)
  {
    const symbol_exprt &symbol_expr=to_symbol_expr(expr);

    exprt resolved=resolve(symbol_expr);

    if(resolved.id()==ID_member ||
       resolved.id()==ID_index)
      resolved=resolve_rec(resolved);

    return resolved;
  }
  else if(expr.id()==ID_member)
  {
    member_exprt member_expr=to_member_expr(expr);

    if(member_expr.struct_op().id()==ID_if)
    {
      const if_exprt &if_expr=to_if_expr(member_expr.struct_op());

      member_expr.struct_op()=if_expr.true_case();
      exprt tmp_true=resolve_rec(member_expr);

      member_expr=to_member_expr(expr);
      member_expr.struct_op()=if_expr.false_case();
      exprt tmp_false=resolve_rec(member_expr);

      if(tmp_true.is_nil())
        return tmp_false;
      else if(tmp_false.is_nil())
        return tmp_true;

      return if_exprt(to_if_expr(to_member_expr(expr).struct_op()).cond(),
                      tmp_true,
                      tmp_false);
    }

    exprt resolved=resolve_rec(member_expr.struct_op());
    if(resolved.is_nil())
      return nil_exprt();

    member_expr.struct_op()=resolved;

    if(resolved.id()==ID_struct ||
       resolved.id()==ID_union)
      simplify(member_expr, ns);

    return member_expr;
  }
  else if(expr.id()==ID_index)
  {
    index_exprt index_expr=to_index_expr(expr);

    if(index_expr.array().id()==ID_if)
    {
      const if_exprt &if_expr=to_if_expr(index_expr.array());

      index_expr.array()=if_expr.true_case();
      exprt tmp_true=resolve_rec(index_expr);

      index_expr=to_index_expr(expr);
      index_expr.array()=if_expr.false_case();
      exprt tmp_false=resolve_rec(index_expr);

      if(tmp_true.is_nil())
        return tmp_false;
      else if(tmp_false.is_nil())
        return tmp_true;

      return if_exprt(to_if_expr(to_index_expr(expr).array()).cond(),
                      tmp_true,
                      tmp_false);
    }

    exprt resolved=resolve_rec(index_expr.array());
    if(resolved.is_nil())
      return nil_exprt();

    index_expr.array()=resolved;

    if(resolved.id()==ID_array)
      simplify(index_expr, ns);

    return index_expr;
  }
  else if(expr.id()==ID_address_of ||
          expr.id()==ID_array ||
          expr.id()==ID_struct ||
          expr.id()==ID_union)
    return expr;
  else
    throw "resolve_rec: "+id2string(expr.id())+" not handled";
}

#if 0
/*******************************************************************\

Function: rd_dereferencet::dereference_member

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

exprt rd_dereferencet::dereference_member(
  const member_exprt &member,
  const exprt &offset,
  const typet &type)
{
  exprt mod_member=member;

  if(member.struct_op().id()==ID_struct ||
     member.struct_op().id()==ID_union)
  {
    if(simplify(mod_member, ns))
      assert(false);

    return dereference_rec(mod_member, offset, type);
  }
  else
  {
    exprt resolved=resolve_rec(member.struct_op());

    if(resolved.is_nil())
      return nil_exprt();

    to_member_expr(mod_member).struct_op()=resolved;

    // TODO - we need to handle more cases
    assert(mod_member!=member);

    return dereference_rec(mod_member, offset, type);
  }

  /*
  // TODO: handle nested member expressions
  else if(member.struct_op().id()==ID_member)
  {
  }
  */
  else if(member.struct_op().id()!=ID_symbol)
    throw "dereference_member: "+id2string(member.struct_op().id())+" not handled";

  exprt &struct_op=to_member_expr(mod_member).struct_op();

  const mp_integer m_offset=
    member_offset(
      to_struct_type(ns.follow(member.struct_op().type())),
      member.get_component_name(),
      ns);
  assert(false); // need to change this code to bits
  const mp_integer m_size=
    pointer_offset_size(member.type(), ns);

  const rd_range_domaint::ranges_at_loct &ranges=
    reaching_definitions[target].get(to_symbol_expr(struct_op).get_identifier());

  exprt result=nil_exprt();

  for(rd_range_domaint::ranges_at_loct::const_iterator
      it=ranges.begin();
      it!=ranges.end();
      ++it)
  {
    for(rd_range_domaint::rangest::const_iterator
        r_it=it->second.begin();
        r_it!=it->second.end();
        ++r_it)
    {
      if(r_it->first>to_range_spect(m_offset+m_size) ||
         r_it->second<to_range_spect(m_offset))
        continue;

      goto_programt::const_targett prev_target=target;
      target=it->first;

      if(target->is_assign())
      {
        if(r_it->first==to_range_spect(m_offset) &&
           r_it->second==to_range_spect(m_offset+m_size))
          mod_member=to_code_assign(target->code).rhs();
        else
        {
          byte_update_little_endian_exprt bu;
          bu.type()=member.struct_op().type();
          bu.op()=member.struct_op();
          bu.offset()=from_integer(r_it->first, index_type());
          bu.value()=to_code_assign(target->code).rhs();

          struct_op=bu;
        }
      }
      else if(target->is_function_call())
      {
        const code_function_callt &function_call=
          to_code_function_call(target->code);

        (void)function_call;

        assert(false);
      }
      else
        assert(false);

      exprt tmp=dereference_rec(mod_member, offset, type);

      target=prev_target;

      if(result.is_nil())
        result=tmp;
      else if(tmp.is_not_nil())
      {
        if(do_guards)
          result=if_exprt(
            equal_exprt(symbol_expr, mod_member),
            tmp,
            result);
        else
          result=if_exprt(
            side_effect_expr_nondett(bool_typet()),
            tmp,
            result);
      }
    }
  }

  return result;
}
#endif

/*******************************************************************\

Function: rd_dereferencet::dereference_rec

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

exprt rd_dereferencet::dereference_rec(
    const exprt &address,
    const exprt &offset,
    const typet &type)
{
  if(address.id()!=ID_symbol &&
     address.id()!=ID_member &&
     address.id()!=ID_index)
    return dereferencet::dereference_rec(address, offset, type);

  exprt result=resolve_rec(address);

  if(result.is_not_nil())
  {
    std::cerr << "DR_REC: " << from_expr(ns, "", result) << std::endl;
    result=dereferencet::dereference_rec(result, offset, type);
  }

  return result;
}
