/*******************************************************************\

Module: Field-sensitive SSA

Author: Michael Tautschnig, mt@eecs.qmul.ac.uk

\*******************************************************************/

#ifndef CPROVER_FIELD_SENSITIVITY_H
#define CPROVER_FIELD_SENSITIVITY_H

class exprt;
class ssa_exprt;
class namespacet;
class goto_symex_statet;
class symex_targett;

/*! \brief Control granularity of object accesses
*/
class field_sensitivityt
{
public:
  void operator()(
    const namespacet &ns,
    exprt &expr,
    bool write) const;

  void get_fields(
    const namespacet &ns,
    const ssa_exprt &ssa_expr,
    exprt &dest) const;

  void field_assignments(
    const namespacet &ns,
    goto_symex_statet &state,
    symex_targett &target,
    const exprt &lhs) const;

  bool is_indivisible(
    const namespacet &ns,
    const ssa_exprt &expr) const;

private:
  void field_assignments_rec(
    const namespacet &ns,
    goto_symex_statet &state,
    symex_targett &target,
    const exprt &lhs_fs,
    const exprt &lhs) const;
};

#endif
