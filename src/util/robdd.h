/*******************************************************************\

Module: Reduced ordered binary decision diagram
        Following Henrik Reif Andersen's "An Introduction to Binary
        Decision Diagrams"

Author: Michael Tautschnig, mt@eecs.qmul.ac.uk

\*******************************************************************/

#ifndef CPROVER_BDD_H
#define CPROVER_BDD_H

/*! \file util/bdd.h
 * \brief Binary decision diagram
 *
 * \author Michael Tautschnig, mt@eecs.qmul.ac.uk
 * \date   Sat Feb 15 22:34:59 GMT 2014
*/

#include <vector>
#include <map>
#include <iosfwd>
#include <deque>

#include "std_expr.h"

class namespacet;

/*! \brief TO_BE_DOCUMENTED
*/
class robddt
{
public:
  explicit robddt(const namespacet &_ns):
    ns(_ns),
    bdd_nodes(root_ref+1, bdd_nodet()),
    var_to_node(2, std::make_pair(exprt(), succ_to_nodet())),
    root(one)
  {
    bdd_nodes[zero].var_number=zero;
    bdd_nodes[one].var_number=one;

    bdd_nodes[root_ref].succ_1=one;

    var_to_node[zero].first=false_exprt();
    var_to_node[zero].second[std::make_pair(invalid, invalid)]=zero;

    var_to_node[one].first=true_exprt();
    var_to_node[one].second[std::make_pair(invalid, invalid)]=one;
  }

  void apply(const exprt &expr);

  void add_var(const exprt &expr);

  void print(std::ostream &os) const;

  exprt as_expr() const;

protected:
  struct bdd_nodet;
  typedef std::vector<bdd_nodet> bdd_nodest;
  typedef bdd_nodest::size_type size_type;

  typedef std::map<std::pair<size_type, size_type>, size_type>
    succ_to_nodet;
  typedef std::vector<std::pair<exprt, succ_to_nodet> > var_to_nodet;

  typedef var_to_nodet::size_type var_numbert;

  enum base_nodet {
    zero=0,
    one=1,
    invalid=2,
    root_ref=3
  };

  struct bdd_nodet
  {
    bdd_nodet():
      var_number((var_numbert)-1),
      succ_0(invalid),
      succ_1(invalid),
      ref_count(0)
    {
    }

    bdd_nodet(var_numbert i, size_type z, size_type o):
      var_number(i),
      succ_0(z),
      succ_1(o),
      ref_count(0)
    {
    }

    var_numbert var_number;
    size_type succ_0, succ_1;
    size_type ref_count;
  };

  const namespacet &ns;

  // contains at least 0, 1, and "invalid" node
  bdd_nodest bdd_nodes;
  std::deque<size_type> free_list;
  var_to_nodet var_to_node;
  size_type root;

  typedef hash_map_cont<exprt, var_numbert, irep_hash>
    ordering_mapt;
  ordering_mapt ordering_map;

  size_type mk(var_numbert i, size_type z, size_type o);
#if 0
  size_type build(const exprt &expr);
  size_type build(const exprt &expr, size_type i);
#endif

  void apply(const exprt &expr, bool negate);
  size_type apply_rec(
    const irep_idt &op,
    size_type r1,
    size_type r2,
    succ_to_nodet &map);

  inline void inc_ref_count(size_type n);
  inline void dec_ref_count(size_type n, bool recurse);

  exprt as_expr(size_type r) const;

#if 0
  void check_consistency(bool strong) const;
#endif
};

#endif
