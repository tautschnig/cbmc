/*******************************************************************\

Module: Reduced ordered binary decision diagram

Author: Michael Tautschnig, mt@eecs.qmul.ac.uk

\*******************************************************************/

#ifdef PRINT_EXPR
#include <langapi/language_util.h>
#endif

#include "simplify_expr.h"
#include "expr_util.h"

#include "robdd.h"

#if 0
void robddt::check_consistency(bool strong) const
{
  std::map<size_type, size_type> in_count;

  for(bdd_nodest::const_iterator it=bdd_nodes.begin();
      it!=bdd_nodes.end();
      ++it)
  {
    const size_type n=it-bdd_nodes.begin();

    if(n<=invalid) continue;

    if(n!=root_ref && it->ref_count==0)
      continue;

    if(n==root_ref)
    {
      assert(it->succ_1!=invalid &&
             it->succ_0==invalid);
      in_count[it->succ_1]++;
    }
    else
    {
      assert(it->succ_1!=invalid &&
             it->succ_0!=invalid);
      in_count[it->succ_1]++;
      in_count[it->succ_0]++;
    }
  }

  for(bdd_nodest::const_iterator it=bdd_nodes.begin();
      it!=bdd_nodes.end();
      ++it)
  {
    const size_type n=it-bdd_nodes.begin();

    if(n<=invalid) continue;

    if((strong && in_count[n]!=it->ref_count) ||
       (!strong && in_count[n]>it->ref_count))
    {
      std::cerr << n << ": ref_count=" << it->ref_count << std::endl;
      std::cerr << n << ": in_count=" << in_count[n] << std::endl;
      assert(false);
    }
  }
}
#endif

/*******************************************************************\

Function: robddt::add_var

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void robddt::add_var(const exprt &expr)
{
  assert(expr.type().id()==ID_bool);

  ordering_mapt::iterator new_entry=
    ordering_map.insert(std::make_pair(expr, 0)).first;

  if(new_entry->second==0)
  {
    new_entry->second=var_to_node.size();
    var_to_node.push_back(std::make_pair(expr, succ_to_nodet()));
  }
}

/*******************************************************************\

Function: robddt::print

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void robddt::print(std::ostream &os) const
{
  os << "digraph G {\n";

  for(bdd_nodest::const_iterator it=bdd_nodes.begin();
      it!=bdd_nodes.end();
      ++it)
  {
    const size_type n=it-bdd_nodes.begin();

    if(n==invalid ||
       n>root_ref && it->ref_count==0)
      continue;

    os << "  n" << (it-bdd_nodes.begin()) << " [";
    if(n==zero || n==one)
      os << "shape=box, ";
    else if(n==root_ref)
      os << "shape=point, color=white, fontcolor=white";

    if(n!=root_ref)
      os << "label=\""
#ifdef PRINT_EXPR
         << from_expr(ns, "", var_to_node[it->var_number].first)
#else
         << it->var_number
#endif
         << "\"";

    os << "];\n";
  }

  for(bdd_nodest::const_iterator it=bdd_nodes.begin()+invalid+1;
      it!=bdd_nodes.end();
      ++it)
  {
    const size_type n=it-bdd_nodes.begin();

    if(n==invalid ||
       n>root_ref && it->ref_count==0)
      continue;

    if(it->succ_0!=invalid)
      os << "  n" << n << " -> " << "n" << it->succ_0
         << " [style=dotted];\n";
    if(it->succ_1!=invalid)
      os << "  n" << n << " -> " << "n" << it->succ_1
         << ";\n";
  }
  os << "}" << std::endl;
}

/*******************************************************************\

Function: robddt::as_expr

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

exprt robddt::as_expr(size_type r) const
{
  if(r<=one)
    return var_to_node[r].first;

  const bdd_nodet &n=bdd_nodes[r];

  assert(n.succ_0!=invalid && n.succ_1!=invalid);

  const exprt &n_expr=var_to_node[n.var_number].first;

  if(n.succ_0==zero)
  {
    if(n.succ_1==one)
      return n_expr;
    else
      return and_exprt(n_expr, as_expr(n.succ_1));
  }
  else if(n.succ_1==zero)
  {
    if(n.succ_0==one)
      return not_exprt(n_expr);
    else
      return and_exprt(not_exprt(n_expr), as_expr(n.succ_0));
  }
  else if(n.succ_0==one)
    return or_exprt(not_exprt(n_expr), as_expr(n.succ_1));
  else if(n.succ_1==one)
    return or_exprt(n_expr, as_expr(n.succ_0));
  else
    return if_exprt(n_expr, as_expr(n.succ_1), as_expr(n.succ_0));
}

/*******************************************************************\

Function: robddt::as_expr

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

exprt robddt::as_expr() const
{
  return as_expr(root);
}

/*******************************************************************\

Function: robddt::inc_ref_count

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void robddt::inc_ref_count(size_type n)
{
  if(n>root_ref)
    bdd_nodes[n].ref_count++;
}

/*******************************************************************\

Function: robddt::dec_ref_count

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void robddt::dec_ref_count(size_type n, bool recurse)
{
  if(n<=root_ref) return;

  bdd_nodet &node=bdd_nodes[n];

  assert(node.ref_count>0);
  node.ref_count--;

  if(recurse && node.ref_count==0)
  {
    dec_ref_count(node.succ_0, recurse);
    dec_ref_count(node.succ_1, recurse);

    succ_to_nodet::iterator entry=var_to_node[node.var_number].second
      .find(std::make_pair(node.succ_0, node.succ_1));
    if(entry!=var_to_node[node.var_number].second.end())
    {
      assert(entry->second==n);
      var_to_node[node.var_number].second.erase(entry);
    }

    free_list.push_back(n);
  }
}

/*******************************************************************\

Function: robddt::apply_rec

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

robddt::size_type robddt::apply_rec(
  const irep_idt &op, size_type r1, size_type r2, succ_to_nodet &map)
{
  assert(r1!=invalid);
  assert(r2!=invalid);

  const std::pair<size_type, size_type> p(r1, r2);

  succ_to_nodet::iterator entry=
    map.insert(std::make_pair(p, (size_type)-1)).first;

  if(entry->second!=(size_type)-1)
  {
    // nothing to do
  }
  else if(r1<=one && r2<=one)
  {
    exprt op_applied(op, bool_typet());
    op_applied.operands().resize(2);

    op_applied.op0()=r1==zero ? static_cast<exprt>(false_exprt()) :
                                static_cast<exprt>(true_exprt());
    op_applied.op1()=r2==zero ? static_cast<exprt>(false_exprt()) :
                                static_cast<exprt>(true_exprt());

    simplify(op_applied, ns);
    assert(op_applied.is_constant());

    entry->second=op_applied.is_false() ? zero : one;
  }
  else if(bdd_nodes[r1].var_number==bdd_nodes[r2].var_number)
  {
    const size_type n1=
      apply_rec(op, bdd_nodes[r1].succ_0, bdd_nodes[r2].succ_0, map);
    inc_ref_count(n1);

    const size_type n2=
         apply_rec(op, bdd_nodes[r1].succ_1, bdd_nodes[r2].succ_1, map);
    inc_ref_count(n2);

    entry->second=mk(bdd_nodes[r1].var_number, n1, n2);

    dec_ref_count(n1, n1!=n2);
    dec_ref_count(n2, n1!=n2);
  }
  else if((r1>one && bdd_nodes[r1].var_number<bdd_nodes[r2].var_number) ||
          r2<=one)
  {
    const size_type n1=apply_rec(op, bdd_nodes[r1].succ_0, r2, map);
    inc_ref_count(n1);

    const size_type n2=apply_rec(op, bdd_nodes[r1].succ_1, r2, map);
    inc_ref_count(n2);

    entry->second=mk(bdd_nodes[r1].var_number, n1, n2);

    dec_ref_count(n1, n1!=n2);
    dec_ref_count(n2, n1!=n2);
  }
  else
  {
    const size_type n1=apply_rec(op, r1, bdd_nodes[r2].succ_0, map);
    inc_ref_count(n1);

    const size_type n2=apply_rec(op, r1, bdd_nodes[r2].succ_1, map);
    inc_ref_count(n2);

    entry->second=mk(bdd_nodes[r2].var_number, n1, n2);

    dec_ref_count(n1, n1!=n2);
    dec_ref_count(n2, n1!=n2);
  }

#if 0
  if(entry->second>one)
  {
    bdd_nodes[entry->second].ref_count++;
    check_consistency(false);
    bdd_nodes[entry->second].ref_count--;
  }
#endif

  return entry->second;
}

/*******************************************************************\

Function: robddt::apply

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void robddt::apply(const exprt &expr, bool negate)
{
  assert(expr.type().id()==ID_bool);

  if(expr.is_constant())
  {
    dec_ref_count(root, true);

    root=negate==expr.is_false() ? one : zero;

    bdd_nodes[root_ref].succ_1=root;
  }
  else if(expr.id()==ID_not)
  {
    apply(to_not_expr(expr).op(), !negate);
  }
  else if(expr.id()==ID_and ||
          expr.id()==ID_or)
  {
    assert(expr.operands().size()>=2);
    exprt bin_expr=make_binary(expr);

    apply(bin_expr.op0(), negate);
    size_type r1=root;
    assert(bdd_nodes[root_ref].succ_1==root);
    // retain this bdd
    inc_ref_count(r1);

    apply(bin_expr.op1(), negate);
    size_type r2=root;
    assert(bdd_nodes[root_ref].succ_1==root);

    succ_to_nodet map;
    root=apply_rec(negate==(expr.id()==ID_and) ? ID_or : ID_and, r1, r2, map);

    bdd_nodes[root_ref].succ_1=root;
    inc_ref_count(root);
    // new we might not need r1, r2 anymore
    dec_ref_count(r1, true);
    dec_ref_count(r2, true);
  }
  else if(expr.id()==ID_if)
  {
    const if_exprt &if_expr=to_if_expr(expr);

    apply(and_exprt(
        or_exprt(not_exprt(if_expr.cond()), if_expr.true_case()),
        or_exprt(if_expr.cond(), if_expr.false_case())), negate);
  }
  else
  {
    ordering_mapt::const_iterator it=ordering_map.find(expr);
    assert(it!=ordering_map.end());

    const size_type root_before=root;

    if(negate)
      root=mk(it->second, one, zero);
    else
      root=mk(it->second, zero, one);

    bdd_nodes[root_ref].succ_1=root;
    inc_ref_count(root);
    dec_ref_count(root_before, true);
  }
}

/*******************************************************************\

Function: robddt::apply

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void robddt::apply(const exprt &expr)
{
#if 0
  check_consistency(true);
  apply(expr, false);
  check_consistency(true);
#else
  apply(expr, false);
#endif
}

/*******************************************************************\

Function: robddt::mk

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

robddt::size_type robddt::mk(var_numbert i, size_type z, size_type o)
{
  assert(i>one);

  if(z==o)
    return z;

  const std::pair<size_type, size_type> p(z, o);
  succ_to_nodet::const_iterator it=var_to_node[i].second.find(p);

  if(it!=var_to_node[i].second.end())
    return it->second;

  inc_ref_count(z);
  inc_ref_count(o);

#if 1
  while(!free_list.empty() &&
        bdd_nodes[free_list.front()].ref_count>0)
    free_list.pop_front();

  if(free_list.empty())
  {
    free_list.push_back(bdd_nodes.size());
    bdd_nodes.push_back(bdd_nodet());
  }
#else
  free_list.push_front(bdd_nodes.size());
  bdd_nodes.push_back(bdd_nodet());
#endif

  const size_type u=free_list.front();
  assert(bdd_nodes[u].ref_count==0);

  bdd_nodes[u].var_number=i;
  bdd_nodes[u].succ_0=z;
  bdd_nodes[u].succ_1=o;

  var_to_node[i].second[p]=u;

  return u;
}

#if 0
/*******************************************************************\

Function: robddt::build

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

robddt::size_type robddt::build(const exprt &expr, var_numbert i)
{
  if(i>=var_to_node.size())
    return expr.is_false() ? zero : one;

  size_type v0=build(set_to_constant(expr, xi, false), i+1);
  size_type v1=build(set_to_constant(expr, xi, true), i+1);

  return mk(i, v0, v1);
}

/*******************************************************************\

Function: robddt::build

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

robddt::size_type robddt::build(const exprt &expr)
{
  return build(expr, 1);
}
#endif
