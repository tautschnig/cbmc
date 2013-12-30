/*******************************************************************\

Module: Find cycles even when no entry function is known

Author: Michael Tautschnig, michael.tautschnig@cs.ox.ac.uk

\*******************************************************************/

#ifndef CPROVER_STATIC_CYCLES_H
#define CPROVER_STATIC_CYCLES_H

class symbol_tablet;
class goto_functionst;

void static_cycles(
  symbol_tablet& symbol_table,
  goto_functionst& goto_functions);

#endif

