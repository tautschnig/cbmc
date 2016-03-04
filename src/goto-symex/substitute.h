/*******************************************************************\

Module: Recursive expression expansion

Author: Michael Tautschnig, mt@eecs.qmul.ac.uk

\*******************************************************************/

#ifndef CPROVER_SUBSTITUTE_H
#define CPROVER_SUBSTITUTE_H

#include <util/expr.h>
#include <util/guard.h>
#include <util/numbering.h>
#include <util/replace_symbol.h>
#include <util/merge_irep.h>

class namespacet;
class symex_targett;
class if_exprt;

class substitutet
{
public:
  substitutet(
    const namespacet &_ns,
    const symex_targett &_target,
    const bool _use_cache):
    ns(_ns),
    target(_target),
    use_cache(_use_cache)
  {
  }

  exprt operator()(const exprt& expr)
  {
    return operator()(expr, false);
  }

  exprt operator()(const exprt& expr, const bool pointers_only)
  {
    return operator()(expr, pointers_only, true, guardt());
  }

protected:
  const namespacet &ns;
  const symex_targett &target;
  const bool use_cache;

  typedef hash_numbering<exprt, irep_hash> expr_numberingt;
  expr_numberingt expr_numbering;

  typedef std::map<unsigned, guardt> expr_mapt;

  exprt operator()(
    const exprt& expr,
    const bool pointers_only,
    const bool do_syms,
    const guardt& guards_seen);

  void merge_expr_maps(expr_mapt &dest, const expr_mapt &other);
  void add_to_expr_map(
    expr_mapt &dest,
    const exprt &expr,
    const guardt &guard);
  void adjust_guards(expr_mapt &dest, const guardt &guard);
#if 0
  void adjust_guards(expr_mapt &dest, const exprt &expr)
  {
    guardt guard;
    guard.add(expr);

    adjust_guards(dest, guard);
  }
#endif

  typedef hash_map_cont<exprt, expr_mapt, irep_hash> subst_cache_innert;
  typedef hash_map_cont<exprt, subst_cache_innert, irep_hash>
    subst_cachet;
  subst_cachet subst_cache_full, subst_cache_pointers_only;

  replace_symbolt replace;

  merge_irept merge_irep;

  void subst_offset(
    const exprt &expr,
    const bool pointers_only,
    const bool recurse,
    const guardt &guards_seen,
    expr_mapt &dest);
  void subst_byte_ops(
    const exprt &expr,
    const guardt &guard,
    expr_mapt &dest);
  void subst_if(
    const if_exprt &expr,
    const bool pointers_only,
    const bool recurse,
    const guardt &guards_seen,
    expr_mapt &dest);
  void subst_rec(
    const exprt &expr,
    const bool pointers_only,
    const bool recurse,
    const guardt &guards_seen,
    expr_mapt &dest);

#if 0
  typedef std::map<std::size_t, hash_set_cont<exprt, irep_hash> > bdd_ordert;
  void add_vars(const exprt &expr);
  void add_vars_rec(const exprt &expr, bdd_ordert &order);
#endif
  void non_redundant_guard(exprt &expr, const guardt &guard);
};

#endif

