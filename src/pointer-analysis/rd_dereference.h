/*******************************************************************\

Module: Points-to analysis using reaching-definitions information

Author: Michael Tautschnig

Date: May 2014

\*******************************************************************/

#ifndef CPROVER_RD_DEREFERENCE_H
#define CPROVER_RD_DEREFERENCE_H

#include <goto-programs/goto_functions.h>

#include "dereference.h"

class reaching_definitions_analysist;

class rd_dereferencet : public dereferencet
{
public:
  virtual ~rd_dereferencet();

  rd_dereferencet(
    const namespacet &_ns,
    const goto_functionst &_goto_functions,
    reaching_definitions_analysist &_rd):
    dereferencet(_ns),
    ns(_ns),
    goto_functions(_goto_functions),
    reaching_definitions(_rd)
  {
  }

  exprt operator()(
    const exprt &pointer,
    goto_programt::const_targett target);

protected:
  const namespacet &ns;
  const goto_functionst &goto_functions;
  reaching_definitions_analysist &reaching_definitions;
  goto_programt::const_targett target;
  typedef hash_map_cont<irep_idt,
                        std::set<goto_programt::const_targett>,
                        irep_id_hash> rec_sett;
  rec_sett rec_set;

  exprt resolve(const symbol_exprt &symbol_expr);
  exprt resolve_rec(const exprt &expr);

  virtual exprt dereference_rec(
    const exprt &address,
    const exprt &offset,
    const typet &type);
};

#endif

