/*******************************************************************\

Module: Recursive expression expansion

Author: Michael Tautschnig, mt@eecs.qmul.ac.uk

\*******************************************************************/

#include <util/std_expr.h>
#include <util/simplify_expr.h>
#include <util/byte_operators.h>
#include <util/arith_tools.h>
#include <util/pointer_offset_size.h>
#include <util/base_type.h>
#include <util/replace_symbol.h>
#include <util/find_symbols.h>
#include <util/prefix.h>
#include <util/replace_expr.h>

#include <ansi-c/c_types.h>

#include <solvers/prop/bdd_expr.h>

#define DEBUG_SUBST
#ifdef DEBUG_SUBST
static int depth=0;
#include <iostream>
#include <langapi/language_util.h>
#endif

#include "symex_target.h"
#include "substitute.h"

#if 0
/*******************************************************************\

Function: substitutet::add_vars

  Inputs:

 Outputs:

 Purpose: Add variables to robdd

\*******************************************************************/

void substitutet::add_vars_rec(const exprt &expr, bdd_ordert &order)
{
  if(expr.id()==ID_constant)
    return;
  else if(expr.id()==ID_and ||
          expr.id()==ID_or ||
          expr.id()==ID_not ||
          expr.id()==ID_if)
  {
    forall_operands(it, expr)
      add_vars_rec(*it, order);
  }
  else if(expr.id()==ID_symbol)
  {
    std::size_t n;
    exprt v=
      target.get_ssa_rhs(to_symbol_expr(expr).get_identifier(), n);
    if(v.is_nil()) n=0;

    order[n].insert(expr);
  }
  else if(expr.type().id()==ID_bool &&
          expr.has_operands() &&
          expr.op0().type().id()!=ID_bool)
  {
    order[0].insert(expr);
  }
  else
  {
    //std::cerr << "Fail: " << from_expr(ns, "", expr) << std::endl;
    assert(false);
  }
}

/*******************************************************************\

Function: substitutet::add_vars

  Inputs:

 Outputs:

 Purpose: Add variables to robdd

\*******************************************************************/

void substitutet::add_vars(const exprt &expr)
{
  bdd_ordert order;
  add_vars_rec(expr, order);

  for(bdd_ordert::const_iterator
      it=order.begin();
      it!=order.end();
      ++it)
    for(hash_set_cont<exprt, irep_hash>::const_iterator
        it2=it->second.begin();
        it2!=it->second.end();
        ++it2)
      robdd.add_var(*it2);
}
#endif

/*******************************************************************\

Function: substitutet::non_redundant_guard

  Inputs:

 Outputs:

 Purpose: Test new guard for redundancy

\*******************************************************************/

void substitutet::non_redundant_guard(exprt &expr, const guardt &guard)
{
  if(guard.is_false())
    expr=false_exprt();

  bdd_exprt t(ns);
  t.from_expr(expr);
  expr=t.as_expr();

  simplify(expr, ns);

  if(expr.is_constant() ||
     guard.is_true())
    return;

  exprt neg_expr=expr.id()==ID_not ?
    to_not_expr(expr).op() : simplify_expr(not_exprt(expr), ns);

  if(guard.as_expr()==expr)
    expr=true_exprt();
  else if(guard.as_expr()==neg_expr)
    expr=false_exprt();
#if 0
  else
  {
    guardt new_guard;
    new_guard.add(expr);
    const guardt::guard_listt &new_guard_list=
      new_guard.get_guard_list();

    exprt::operandst new_ops;
    new_ops.reserve(new_guard_list.size());

    const guardt::guard_listt &guard_list=guard.get_guard_list();
    for(guardt::guard_listt::const_iterator
        itn=new_guard_list.begin();
        itn!=new_guard_list.end();
        ++itn)
    {
      exprt e=*itn;
      bool found=false;

      for(guardt::guard_listt::const_iterator
          it=guard_list.begin();
          !found && it!=guard_list.end();
          ++it)
      {
        if(*it==e)
        {
          found=true;
        }
        else if(e.id()==ID_not && *it==to_not_expr(e).op())
        {
          expr=false_exprt();
          return;
        }
        else if(e.id()!=ID_not && *it==not_exprt(e))
        {
          expr=false_exprt();
          return;
        }
        else if(*it==neg_expr)
        {
          expr=false_exprt();
          return;
        }
      }

      if(e.id()==ID_or)
      {
        Forall_operands(ite, e)
        {
          for(guardt::guard_listt::const_iterator
              it=guard_list.begin();
              !found && it!=guard_list.end();
              ++it)
          {
            if(*it==*ite)
            {
              found=true;
            }
            else if(ite->id()==ID_not && *it==to_not_expr(*ite).op())
            {
              *ite=false_exprt();
            }
            else if(ite->id()!=ID_not && *it==not_exprt(*ite))
            {
              *ite=false_exprt();
            }
          }
        }

        if(!found)
          simplify(e, ns);

        if(e.is_false())
        {
          expr=false_exprt();
          return;
        }
      }

      if(!found)
        new_ops.push_back(e);
    }

    expr=conjunction(new_ops);
  }
#endif
}

#if 0
/*******************************************************************\

Function: simplify_if_rec

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

static void simplify_if(exprt &expr)
{
  if(expr.id()!=ID_if)
  {
    Forall_operands(it, expr)
      simplify_if_rec(*it);
  }
  else
  {
    if_exprt &if_expr=to_if_expr(expr);

    if(if_expr.cond().is_true())
      simplify_if_rec(if_expr.true_case());
    else if(if_expr.cond().is_false())
      simplify_if_rec(if_expr.false_case());
    else
    {
      // from simplify_exprt::simplify_if - disabled in there as it
      // is too costly in general

      // a ? b : c  --> a ? b[a/true] : c
      replace_expr(if_expr.cond(), true_exprt(), if_expr.true_case());
      simplify_if(if_expr.true_case());

      // a ? b : c  --> a ? b : c[a/false]
      replace_expr(if_expr.cond(), false_exprt(), if_expr.false_case());
      simplify_if(if_expr.false_case());
    }
  }
}
#endif

/*******************************************************************\

Function: substitutet::operator()

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

exprt substitutet::operator()(
  const exprt& expr,
  const bool pointers_only,
  const bool do_syms,
  const guardt &guards_seen)
{
  if(do_syms)
  {
#ifdef DEBUG_SUBST
  std::cerr << std::string(2*depth, ' ') << depth
            << ": operator() got " << from_expr(ns, "", expr)
            << " [" << from_type(ns, "", expr.type()) << "]" << std::endl;
#endif
  std::list<irep_idt> to_expand;
  typedef std::map<std::size_t, std::pair<irep_idt, exprt> >
    ordered_expandt;
  ordered_expandt ordered_expand;
  find_symbols_sett on_stack;

  find_symbols_sett syms;
  find_symbols(expr, syms);
  to_expand.insert(to_expand.end(), syms.begin(), syms.end());

  while(!to_expand.empty())
  {
    const irep_idt identifier=to_expand.front();
    to_expand.pop_front();

    if(replace.expr_map.find(identifier)!=replace.expr_map.end())
      continue;
    else if(!on_stack.insert(identifier).second)
      continue;
    else if(has_prefix(id2string(identifier), "goto_symex::\\guard"))
      continue;
    std::cerr << "identifier=" << identifier << std::endl;

    std::size_t n;
    exprt v=target.get_ssa_rhs(identifier, n);
    if(v.is_nil())
      continue;

    std::cerr << identifier << " --> " << from_expr(ns, "", v) << std::endl;
    if(!ordered_expand.insert(std::make_pair(n, std::make_pair(identifier, v))).second)
      assert(false);

    syms.clear();
    find_symbols(v, syms);
    to_expand.insert(to_expand.end(), syms.begin(), syms.end());
  }

  for(ordered_expandt::iterator
      it=ordered_expand.begin();
      it!=ordered_expand.end();
      ++it)
  {
    const irep_idt &identifier=it->second.first;
    exprt &expr=it->second.second;

    std::cerr << "Sub " << identifier << /*" -> " << from_expr(ns, "", expr) <<*/ std::endl;
    std::cerr << "Replacing in " << from_expr(ns, "", expr) << std::endl;
    replace.replace(expr);
    // pushing if conditions inside is really essential
    //simplify_if(expr);
    simplify(expr, ns);

#if 0
    if(expr.id()==ID_if)
    {
      if_exprt if_expr=to_if_expr(expr);
      add_vars(if_expr.cond());
      robdd.apply(if_expr.cond());
      if_expr.cond()=robdd.as_expr();

      simplify(if_expr.cond(), ns);
      static int tol_if=0;
      if(expr!=if_expr)
      {
        std::cerr << "expr=" << from_expr(ns, "", expr) << std::endl;
        std::cerr << "if_expr=" << from_expr(ns, "", if_expr) << std::endl;
        ++tol_if;
        assert(tol_if<1);
      }
    }
#endif
#if 1
    expr_mapt expr_map;

    subst_rec(expr, pointers_only, true, guards_seen, expr_map);

    assert(!expr_map.empty());
    exprt expr_mod=expr_numbering[expr_map.begin()->first];
    std::cerr << "expr_mod=" << from_expr(ns, "", expr_mod) << std::endl;

    for(expr_mapt::const_iterator it=++(expr_map.begin());
        it!=expr_map.end();
        ++it)
    {
      exprt e=expr_numbering[it->first];
      std::cerr << "e=" << from_expr(ns, "", e) << std::endl;

      exprt guard_expr=it->second.as_expr();
      non_redundant_guard(guard_expr, guardt());

      expr_mod=if_exprt(guard_expr, e, expr_mod);
    }
    simplify(expr_mod, ns);
    static int tol_if=0;
    if(expr!=expr_mod)
    {
      std::cerr << "expr=" << from_expr(ns, "", expr) << std::endl;
      std::cerr << "expr_mod=" << from_expr(ns, "", expr_mod) << std::endl;
      ++tol_if;
      //assert(tol_if<7);
      expr=expr_mod;
    }
#endif
    std::cerr << "Internal replace done: " << identifier << std::endl;
    std::cerr << "Maps to: " << from_expr(ns, "", expr) << std::endl;
    //assert(identifier!="main::1::node!0@1#2");

    merge_irep(expr);
    replace.insert(identifier, expr);
  }
  }

  exprt result=expr;

  if(do_syms)
  {
#if 0
  std::cerr << "Before replace=" << from_expr(ns, "", result) << std::endl;
  replace.replace(result);
  std::cerr << "Replace once=" << from_expr(ns, "", result) << std::endl;
  if(!replace.replace(result))
  {
    std::cerr << "Replace twice=" << from_expr(ns, "", result) << std::endl;
    assert(false);
  }
  assert(replace.replace(result));
#else
  for(bool done=false;
      !done;
      done=replace.replace(result)) ;
#endif
  }

#if 1
  expr_mapt expr_map;

  subst_rec(result, pointers_only, true, guards_seen, expr_map);

  assert(!expr_map.empty());
  exprt result2=expr_numbering[expr_map.begin()->first];

  for(expr_mapt::const_iterator it=++(expr_map.begin());
      it!=expr_map.end();
      ++it)
  {
    exprt e=expr_numbering[it->first];

    exprt guard_expr=it->second.as_expr();
    non_redundant_guard(guard_expr, guardt());

    result2=if_exprt(guard_expr, e, result2);
  }
#else
  exprt result2=result;
#endif

  if(do_syms)
  {
#ifdef DEBUG_SUBST
  std::cerr << std::string(2*depth, ' ') << depth
            << ": operator()=" << from_expr(ns, "", simplify_expr(result, ns))
            << " [" << from_type(ns, "", result.type()) << "]" << std::endl;
#if 1
  std::cerr << std::string(2*depth, ' ') << depth
            << ": subst operator()=" << from_expr(ns, "", simplify_expr(result2, ns))
            << " [" << from_type(ns, "", result.type()) << "]" << std::endl;
  /*static int tol=0;
  if(simplify_expr(result, ns)!=simplify_expr(result2, ns))
  {
    ++tol;
    assert(tol<8);
  }*/
#endif
#endif
  }

  return result2;
}

/*******************************************************************\

Function: substitutet::subst_rec

  Inputs:

 Outputs:

 Purpose: Expand symbols recursively

\*******************************************************************/

void substitutet::subst_rec(
  const exprt &expr,
  const bool pointers_only,
  const bool recurse,
  const guardt &guards_seen,
  expr_mapt &dest)
{
#ifdef DEBUG_SUBST
  std::cerr << std::string(2*depth, ' ') << depth
            << ": subst_rec: " << from_expr(ns, "", expr)
            << "{g_seen=" << from_expr(ns, "", guards_seen.as_expr()) << "} "
            << " [" << from_type(ns, "", expr.type()) << ", p_only="
            << pointers_only << "]" << std::endl;
  ++depth;
#endif

#if 0
  subst_cache_innert::iterator cache_it;
  if(use_cache)
  {
    // look up the value of expr in the cache
    subst_cache_innert &cache=
      (pointers_only?subst_cache_pointers_only:subst_cache_full)[expr];
    cache_it=cache.end();

    const exprt guards_seen_expr=guards_seen.as_expr();

    for(subst_cache_innert::iterator it=cache.begin();
        it!=cache.end();
        ++it)
    {
      if(it->second.empty()) continue;

#ifdef DEBUG_SUBST
      std::cerr << std::string(2*depth, ' ') << depth
        << ": trying: "
        << "{g_seen=" << from_expr(ns, "", it->first) << "}"
        << std::endl;
#endif

      guardt diff;
      diff.add(it->first);
      diff-=guards_seen;

      if(diff.is_true())
      {
#ifdef DEBUG_SUBST
        std::cerr << "FOUND!!!" << std::endl;
#endif
        cache_it=it;
        break;
      }

      add_vars(guards_seen_expr);
      add_vars(it->first);
      robdd.apply(or_exprt(not_exprt(guards_seen_expr), it->first));
      if(robdd.as_expr().is_true())
      {
#ifdef DEBUG_SUBST
        std::cerr << "FOUND BY BDD!!!" << std::endl;
#endif
        cache_it=it;
        break;
      }
    }

    if(cache_it!=cache.end())
    {
      merge_expr_maps(dest, cache_it->second);

#ifdef DEBUG_SUBST
      --depth;
      std::cerr << std::string(2*depth, ' ') << depth << ": cached" << std::endl;
#endif

      return;
    }

    cache_it=
      cache.insert(
        std::make_pair(guards_seen_expr, expr_mapt())).first;


#ifdef DEBUG_SUBST
    subst_cachet::const_iterator entry=subst_cache_pointers_only.find(expr);
    if(entry!=subst_cache_pointers_only.end())
      for(subst_cache_innert::const_iterator it=entry->second.begin();
          it!=entry->second.end();
          ++it)
      {
        if(it->second.empty()) continue;

        std::cerr << std::string(2*depth, ' ') << depth
          << ": pointers_only: "
          << "{g_seen=" << from_expr(ns, "", it->first) << "}"
          << std::endl;
      }

    entry=subst_cache_full.find(expr);
    if(entry!=subst_cache_full.end())
      for(subst_cache_innert::const_iterator it=entry->second.begin();
          it!=entry->second.end();
          ++it)
      {
        if(it->second.empty()) continue;

        std::cerr << std::string(2*depth, ' ') << depth
          << ": full: "
          << "{g_seen=" << from_expr(ns, "", it->first) << "}"
          << std::endl;
      }
#endif
  }
#endif

  expr_mapt expr_map;

  if(!pointers_only || expr.type().id()==ID_pointer)
  {
    if(expr.id()==ID_index ||
       expr.id()==ID_member ||
       expr.id()==ID_with ||
       expr.id()==ID_byte_extract_little_endian ||
       expr.id()==ID_byte_extract_big_endian /*||
       expr.id()==ID_byte_update_little_endian ||
       expr.id()==ID_byte_update_big_endian*/)
    {
      subst_offset(expr, pointers_only, recurse, guards_seen, expr_map);
    }
#if 0
    else if(expr.id()==ID_symbol)
    {
      const irep_idt identifier=to_symbol_expr(expr).get_identifier();

      std::size_t ignored;
      exprt v=target.get_ssa_rhs(identifier, ignored);

      if(v.is_not_nil())
      {
        if(recurse)
          subst_rec(v, pointers_only, recurse, guards_seen, expr_map);
        else
          add_to_expr_map(expr_map, v, guardt());
      }
      else
        add_to_expr_map(expr_map, expr, guardt());
    }
#endif
    else if(expr.id()==ID_typecast)
    {
      const typecast_exprt &tc=to_typecast_expr(expr);

      const bool expand_full=
        !pointers_only ||
        tc.op().type().id()!=ID_pointer;

      expr_mapt tc_map;
      subst_rec(tc.op(), !expand_full, recurse, guards_seen, tc_map);

      for(expr_mapt::const_iterator it=tc_map.begin();
          it!=tc_map.end();
          ++it)
      {
        exprt o=expr_numbering[it->first];

        add_to_expr_map(
          expr_map,
          typecast_exprt(o, expr.type()),
          it->second);
      }
    }
    else if(expr.id()==ID_if)
    {
      subst_if(to_if_expr(expr), pointers_only, recurse, guards_seen, expr_map);
    }
    else if(expr.id()!=ID_address_of && expr.has_operands())
    {
      exprt result=expr;
      exprt::operandst::iterator r_it=result.operands().begin();

      forall_operands(it, expr)
      {
        if(it->id()!=ID_constant &&
           (!pointers_only || it->type().id()==ID_pointer))
          *r_it=operator()(*it, pointers_only, false, guards_seen);
        ++r_it;
      }
      result.remove(ID_C_expr_simplified);

      add_to_expr_map(expr_map, result, guardt());
    }
    else
      add_to_expr_map(expr_map, expr, guardt());
  }
  else
    add_to_expr_map(expr_map, expr, guardt());

#ifdef DEBUG_SUBST
  --depth;
  for(expr_mapt::const_iterator it=expr_map.begin();
      it!=expr_map.end();
      ++it)
  {
    exprt o=expr_numbering[it->first];
    std::cerr << std::string(2*depth, ' ') << depth << ": subst_rec "
      << expr.id() << " result cand: "
      << "{g=" << from_expr(ns, "", it->second.as_expr()) << "} "
      << from_expr(ns, "", o)
      << " [" << from_type(ns, "", o.type()) << "]" << std::endl;
  }
#endif

  merge_expr_maps(dest, expr_map);

#if 0
  if(use_cache && recurse)
    cache_it->second.swap(expr_map);
#endif
}

/*******************************************************************\

Function: substitutet::subst_if

  Inputs:

 Outputs:

 Purpose: Expand op1, op2 in c ? op1 : op2

\*******************************************************************/

void substitutet::subst_if(
  const if_exprt &expr,
  const bool pointers_only,
  const bool recurse,
  const guardt &guards_seen,
  expr_mapt &dest)
{
  exprt simp_cond=expr.cond();
  non_redundant_guard(simp_cond, guards_seen);

  exprt simp_neg_cond=not_exprt(simp_cond);
  non_redundant_guard(simp_neg_cond, guards_seen);

  expr_mapt true_map;
  if(!simp_cond.is_false())
  {
    if(!recurse)
      add_to_expr_map(true_map, expr.true_case(), guardt());
    else
    {
      guardt true_guards=guards_seen;
      true_guards.add(simp_cond);
      subst_rec(expr.true_case(), pointers_only, recurse, true_guards, true_map);
    }
  }

  expr_mapt false_map;
  if(!simp_neg_cond.is_false())
  {
    if(!recurse)
      add_to_expr_map(false_map, expr.false_case(), guardt());
    else
    {
      guardt false_guards=guards_seen;
      false_guards.add(simp_neg_cond);
      subst_rec(expr.false_case(), pointers_only, recurse, false_guards, false_map);
    }
  }

  // a refined variant of merge_expr_maps
  for(expr_mapt::iterator
      itt=true_map.begin(), itf=false_map.begin();
      itt!=true_map.end() || itf!=false_map.end();
      ) // no itt/itf++
  {
    if(itt!=true_map.end() &&
       (itf==false_map.end() || itt->first<itf->first))
    {
      exprt tmp=simp_cond;
      non_redundant_guard(tmp, itt->second);
      if(!tmp.is_false())
      {
        itt->second.add(tmp);
        ++itt;
      }
      else
      {
        expr_mapt::iterator cur=itt;
        ++itt;
        true_map.erase(cur);
      }
      continue;
    }
    if(itf!=false_map.end() &&
       (itt==true_map.end() || itf->first<itt->first))
    {
      exprt tmp=simp_neg_cond;
      non_redundant_guard(tmp, itf->second);
      if(!tmp.is_false())
      {
        itf->second.add(tmp);
        true_map.insert(itt, *itf);
      }
      ++itf;
      continue;
    }

    // both true and false part yield the same expression -
    // merge the guards
    assert(itt->first==itf->first);

    guardt true_guard=itt->second;
    guardt false_guard=itf->second;

    bdd_exprt t(ns);
    t.from_expr(or_exprt(true_guard, false_guard));
    itt->second=t.as_expr();

    ++itt;
    ++itf;
  }

  merge_expr_maps(dest, true_map);
}

/*******************************************************************\

Function: substitutet::subst_offset

  Inputs:

 Outputs:

 Purpose: Expand op in op.member, op[index], byte_extract(op, ...)

\*******************************************************************/

void substitutet::subst_offset(
  const exprt &expr,
  const bool pointers_only,
  const bool recurse,
  const guardt &guards_seen,
  expr_mapt &dest)
{
  expr_mapt expr_map;

  // ID_member, ID_index, ID_with, ID_byte_update_*, ID_byte_extract_*
  // all have the base object as op0() -- expand once
  expr_mapt op_map;
  subst_rec(expr.op0(), false, false, guards_seen, op_map);

  expr_mapt other_op_map, op2_map;

  // If the update value of ID_with is a pointer, this needs to be
  // expanded as well
  if(expr.id()==ID_with)
    subst_rec(to_with_expr(expr).new_value(), pointers_only, true, guards_seen, other_op_map);
  // Extracting a pointer via byte extract requires an exact index
  else if((expr.id()==ID_byte_extract_big_endian ||
           expr.id()==ID_byte_extract_little_endian) &&
          (!pointers_only || expr.type().id()==ID_pointer))
    subst_rec(to_byte_extract_expr(expr).offset(), false, true, guards_seen, other_op_map);
  // Updating a pointer via byte update should have precise index information
  else if((expr.id()==ID_byte_update_big_endian ||
           expr.id()==ID_byte_update_little_endian) &&
          (!pointers_only || expr.op2().type().id()==ID_pointer))
  {
    subst_rec(expr.op1(), false, true, guards_seen, other_op_map);
    subst_rec(expr.op2(), pointers_only, true, guards_seen, op2_map);
  }

  for(expr_mapt::const_iterator it=op_map.begin();
      it!=op_map.end();
      ++it)
  {
    exprt o=expr_numbering[it->first];

    // An index into a concrete array; we add a guard for each of the
    // valid indices
    if(o.id()==ID_array &&
       expr.id()==ID_index)
    {
      assert(!o.operands().empty());

      const index_exprt &idx=to_index_expr(expr);

      mp_integer i=0;
      forall_operands(ito, o)
      {
        equal_exprt index_eq(
          from_integer(i, idx.index().type()), idx.index());

        guardt guard=it->second;
        non_redundant_guard(index_eq, guard);
        guard.add(index_eq);

        add_to_expr_map(expr_map, *ito, guard);

        ++i;
      }
    }
    // We might have to do a Cartesian combination with another op
    else if(!other_op_map.empty())
    {
      exprt tmp=expr;
      tmp.op0()=o;

      for(expr_mapt::const_iterator ito=other_op_map.begin();
          ito!=other_op_map.end();
          ++ito)
      {
        exprt oo=expr_numbering[ito->first];
        exprt result=tmp;

        if(result.id()==ID_with)
          to_with_expr(result).new_value()=oo;
        else if(result.id()==ID_byte_extract_big_endian ||
                result.id()==ID_byte_extract_little_endian)
          to_byte_extract_expr(result).offset()=oo;
        else if(result.id()==ID_byte_update_big_endian ||
                result.id()==ID_byte_update_little_endian)
          result.op1()=oo;
        else
          assert(false);
        result.remove(ID_C_expr_simplified);

        guardt guard=it->second;
        guard.add(ito->second);

        if(op2_map.empty())
        {
          //simplify(result, ns);
          subst_byte_ops(result, guard, expr_map);
        }
        else
        {
          for(expr_mapt::const_iterator ito2=op2_map.begin();
              ito2!=op2_map.end();
              ++ito2)
          {
            result.op2()=expr_numbering[ito2->first];
            result.remove(ID_C_expr_simplified);

            guard.add(ito2->second);

            //simplify(result, ns);
            subst_byte_ops(result, guard, expr_map);
          }
        }
      }
    }
    // Otherwise try various operations at byte level
    else
    {
      exprt result=expr;
      result.op0()=o;
      result.remove(ID_C_expr_simplified);

      //simplify(result, ns);
      subst_byte_ops(result, it->second, expr_map);
    }
  }

  if(!recurse ||
     (expr_map.size()==1 &&
      expr_numbering[expr_map.begin()->first]==expr))
    merge_expr_maps(dest, expr_map);
  else
  {
    for(expr_mapt::const_iterator it=expr_map.begin();
        it!=expr_map.end();
        ++it)
    {
      exprt o=expr_numbering[it->first];

      guardt g=guards_seen;
      g.append(it->second);

      expr_mapt expr_map2;
      subst_rec(o, pointers_only, recurse, g, expr_map2);
      adjust_guards(expr_map2, it->second);
      merge_expr_maps(dest, expr_map2);
    }
  }
}

/*******************************************************************\

Function: substitutet::subst_byte_ops

  Inputs:

 Outputs:

 Purpose: Expand byte_extract(...) or expr(byte_update_*())

\*******************************************************************/

void substitutet::subst_byte_ops(
  const exprt &expr,
  const guardt &guard,
  expr_mapt &dest)
{
#if 1
  const exprt &op=expr.op0();
  const typet &op_type=ns.follow(op.type());

  // When a byte update does not affect the member/index we are trying
  // to substitute for, recurse to its root object. This case is only
  // required when the byte update offset is non-constant (otherwise
  // simplify would have taken care of this), thus we build suitable 
  // guard expressions.
  if((op.id()==ID_byte_update_little_endian &&
      byte_extract_id()==ID_byte_extract_little_endian) ||
     (op.id()==ID_byte_update_big_endian &&
      byte_extract_id()==ID_byte_extract_big_endian))
  {
    object_descriptor_exprt ode;
    ode.build(expr, ns);

    exprt update_size=size_of_expr(op.op2().type(), ns);
    assert(update_size.is_not_nil());

    exprt lb=plus_exprt(
      typecast_exprt(op.op1(), index_type()),
      update_size);

    exprt extract_size=size_of_expr(expr.type(), ns);
    assert(extract_size.is_not_nil());

    exprt ub=plus_exprt(ode.offset(), extract_size);

    exprt no_update_bounds=or_exprt(
      binary_relation_exprt(ode.offset(), ID_ge, lb),
      binary_relation_exprt(ub, ID_le, op.op1()));
    non_redundant_guard(no_update_bounds, guard);

    exprt in_update_bounds=and_exprt(
      binary_relation_exprt(ode.offset(), ID_ge, op.op1()),
      binary_relation_exprt(ub, ID_le, lb));
    non_redundant_guard(in_update_bounds, guard);

    if(!no_update_bounds.is_false() &&
       !in_update_bounds.is_true())
    {
      exprt no_update=expr;
      no_update.op0()=op.op0();
      no_update.remove(ID_C_expr_simplified);

      guardt g_no_update_bounds=guard;
      g_no_update_bounds.add(no_update_bounds);
      subst_byte_ops(no_update, g_no_update_bounds, dest);
    }

    if(!in_update_bounds.is_false() &&
       !no_update_bounds.is_true())
    {
      object_descriptor_exprt ode2;
      ode2.offset()=minus_exprt(ode.offset(), op.op1());
      ode2.build(op.op2(), ns);

      byte_extract_exprt be(byte_extract_id());
      be.type()=expr.type();
      be.op()=ode2.root_object();
      be.offset()=simplify_expr(ode2.offset(), ns);

      guardt g_in_update_bounds=guard;
      g_in_update_bounds.add(in_update_bounds);
      add_to_expr_map(dest, be, g_in_update_bounds);
    }
  }
  // Similar to byte_update: see whether ID_with overlaps;
  // ID_update would require iteration over all its designator ops
  else if((expr.id()==ID_byte_extract_big_endian ||
           expr.id()==ID_byte_extract_little_endian) &&
          op.id()==ID_with)
  {
    object_descriptor_exprt ode;
    ode.build(expr, ns);

    exprt update_size=size_of_expr(op.op2().type(), ns);
    assert(update_size.is_not_nil());

    exprt update_offset;
    if(op.op1().id()==ID_member_name)
      update_offset=
        member_offset_expr(to_struct_type(op.op0().type()),
                           op.op1().get(ID_component_name),
                           ns);
    else
      update_offset=
        mult_exprt(op.op1(), size_of_expr(op.op0().type().subtype(), ns));

    exprt lb=plus_exprt(
      typecast_exprt(update_offset, index_type()),
      update_size);

    exprt extract_size=size_of_expr(expr.type(), ns);
    assert(extract_size.is_not_nil());

    exprt ub=plus_exprt(ode.offset(), extract_size);

    exprt no_update_bounds=or_exprt(
      binary_relation_exprt(ode.offset(), ID_ge, lb),
      binary_relation_exprt(ub, ID_le, update_offset));
    non_redundant_guard(no_update_bounds, guard);

    exprt in_update_bounds=and_exprt(
      binary_relation_exprt(ode.offset(), ID_ge, update_offset),
      binary_relation_exprt(ub, ID_le, lb));
    non_redundant_guard(in_update_bounds, guard);

    if(!no_update_bounds.is_false() &&
       !in_update_bounds.is_true())
    {
      exprt no_update=expr;
      no_update.op0()=op.op0();
      no_update.remove(ID_C_expr_simplified);

      guardt g_no_update_bounds=guard;
      g_no_update_bounds.add(no_update_bounds);
      subst_byte_ops(no_update, g_no_update_bounds, dest);
    }

    if(!in_update_bounds.is_false() &&
       !no_update_bounds.is_true())
    {
      object_descriptor_exprt ode2;
      ode2.offset()=minus_exprt(ode.offset(), update_offset);
      ode2.build(op.op2(), ns);

      byte_extract_exprt be(byte_extract_id());
      be.type()=expr.type();
      be.op()=ode2.root_object();
      be.offset()=simplify_expr(ode2.offset(), ns);

      guardt g_in_update_bounds=guard;
      g_in_update_bounds.add(in_update_bounds);
      add_to_expr_map(dest, be, g_in_update_bounds);
    }
  }
  // try extracting the full object -- offset must be zero
  else if(byte_extract_id()==expr.id() &&
          (base_type_eq(expr.type(), op_type, ns) ||
           (op_type.id()==ID_pointer &&
            ns.follow(expr.type()).id()==ID_pointer)))
  {
    equal_exprt is_zero(expr.op1(), from_integer(0, index_type()));

    non_redundant_guard(is_zero, guard);
    guardt guard_zero=guard;
    guard_zero.add(is_zero);

    if(base_type_eq(op_type, expr.type(), ns))
      add_to_expr_map(dest, op, guard_zero);
    else
      add_to_expr_map(dest, typecast_exprt(op, expr.type()), guard_zero);
  }
  // try array of pointers
  // offset must be divisible by size of subtype without remainder
  else if(byte_extract_id()==expr.id() &&
          op_type.id()==ID_array &&
          (base_type_eq(op_type.subtype(), expr.type(), ns) ||
           (ns.follow(op_type.subtype()).id()==ID_pointer &&
            ns.follow(expr.type()).id()==ID_pointer)))
  {
    exprt size_of=size_of_expr(expr.type(), ns);
    assert(size_of.is_not_nil());

    equal_exprt no_remainder(mod_exprt(expr.op1(), size_of),
                             from_integer(0, index_type()));

    index_exprt index(op, div_exprt(expr.op1(), size_of));

    non_redundant_guard(no_remainder, guard);
    guardt guard_nr=guard;
    guard_nr.add(no_remainder);

    if(base_type_eq(op_type.subtype(), expr.type(), ns))
      add_to_expr_map(dest, index, guard_nr);
    else
      add_to_expr_map(dest, typecast_exprt(index, expr.type()), guard_nr);
  }
  // or array of arrays -- refine byte_extract expression
  else if(byte_extract_id()==expr.id() &&
          op_type.id()==ID_array &&
          op_type.subtype().id()==ID_array)
  {
    exprt size_of_subt=size_of_expr(op_type.subtype(), ns);
    assert(size_of_subt.is_not_nil());

    index_exprt index(op, div_exprt(expr.op1(), size_of_subt));

    mod_exprt offset(expr.op1(), size_of_subt);

    exprt result=expr;
    result.op0()=index;
    result.op1()=offset;
    result.remove(ID_C_expr_simplified);

    add_to_expr_map(dest, result, guard);
  }
  // try struct/union member
  else if(byte_extract_id()==expr.id() &&
          (op_type.id()==ID_struct ||
           op_type.id()==ID_union))
  {
    const struct_union_typet &type=to_struct_union_type(op_type);
    const struct_union_typet::componentst &comp=type.components();

    exprt result=nil_exprt();
    guardt guard_zero=guard;

    for(struct_union_typet::componentst::const_iterator
        it=comp.begin();
        it!=comp.end();
        ++it)
    {
      const typet &m_type=ns.follow(it->type());

      member_exprt member(op, it->get_name(), m_type);

      if(base_type_eq(expr.type(), m_type, ns))
      {
        if(op_type.id()==ID_union)
        {
          // offset must be zero
          equal_exprt is_zero(expr.op1(), from_integer(0, index_type()));

          non_redundant_guard(is_zero, guard_zero);
          guard_zero.add(is_zero);
          result=member;

          // there could be lots of ambiguity here, we just use the
          // first one
          break;
        }
        else
        {
          // test whether offsets match
          exprt offset=member_offset_expr(
            to_struct_type(op_type), it->get_name(), ns);
          assert(offset.is_not_nil());
          equal_exprt is_offset(offset, expr.op1());
          simplify(is_offset, ns);

          if(is_offset.is_false())
            continue;

          result=if_exprt(is_offset, member, result);
        }
      }
      else if(m_type.id()==ID_array ||
              m_type.id()==ID_struct ||
              m_type.id()==ID_union)
      {
        if(op_type.id()==ID_union)
        {
          result=expr;
          result.op0()=member;
          result.remove(ID_C_expr_simplified);

          // there could be lots of ambiguity here, we just use the
          // first one
          break;
        }
        else
        {
          // test whether offset could be this large
          exprt offset=member_offset_expr(
            to_struct_type(op_type), it->get_name(), ns);
          assert(offset.is_not_nil());
          binary_relation_exprt offset_le(offset, ID_le, expr.op1());
          simplify(offset_le, ns);

          if(offset_le.is_false())
            continue;

          byte_extract_exprt be=to_byte_extract_expr(expr);
          be.op()=member;
          be.offset()=minus_exprt(expr.op1(), offset);
          be.remove(ID_C_expr_simplified);

          result=if_exprt(offset_le, be, result);
        }
      }
    }

    assert(result.is_not_nil());
    add_to_expr_map(dest, result, guard_zero);
  }
  else
#endif
    add_to_expr_map(dest, expr, guard);
}

/*******************************************************************\

Function: substitutet::merge_expr_maps

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void substitutet::merge_expr_maps(
  expr_mapt &dest,
  const expr_mapt &other)
{
  expr_mapt::iterator it=dest.begin();
  for(expr_mapt::const_iterator
      ito=other.begin();
      ito!=other.end();
      ++ito)
  {
    while(it!=dest.end() && it->first<ito->first)
      ++it;
    if(it==dest.end() || ito->first<it->first)
      dest.insert(it, *ito);
    else if(it!=dest.end())
    {
      assert(it->first==ito->first);
      it->second|=ito->second;
      ++it;
    }
  }
}

/*******************************************************************\

Function: substitutet::add_to_expr_map

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void substitutet::add_to_expr_map(
  expr_mapt &dest,
  const exprt &expr,
  const guardt &guard)
{
  if(guard.is_false())
    return;

  const unsigned number=expr_numbering.number(simplify_expr(expr, ns));

  std::pair<expr_mapt::iterator,bool> entry=
    dest.insert(std::make_pair(number, guard));

  if(!entry.second)
    entry.first->second|=guard;
}

/*******************************************************************\

Function: substitutet::adjust_guards

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void substitutet::adjust_guards(expr_mapt &dest, const guardt &guard)
{
  for(expr_mapt::iterator it=dest.begin();
      it!=dest.end();
      ++it)
    it->second.add(guard);
}
