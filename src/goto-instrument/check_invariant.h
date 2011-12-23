/*******************************************************************\

Module: Invariant Instrumentation

Author: Michael Tautschnig, Daniel Kroening

Date: December 2011

\*******************************************************************/

#ifndef CPROVER_GOTO_CHECK_INVARIANT_H
#define CPROVER_GOTO_CHECK_INVARIANT_H

#include <util/irep.h>

class symbol_tablet;
class value_setst;
class goto_functionst;

void invariant(
  value_setst &value_sets,
  const class symbol_tablet &symbol_table,
  goto_functionst &goto_functions,
  const irep_idt &invariant_check);

#endif
