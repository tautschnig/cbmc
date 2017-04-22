/*******************************************************************\

Module: Perform loop transformations as commonly done in compilers

Author: Michael Tautschnig

Date: April 2016

\*******************************************************************/

#ifndef CPROVER_GOTO_INSTRUMENT_LOOP_TRANSFORMS_H
#define CPROVER_GOTO_INSTRUMENT_LOOP_TRANSFORMS_H

class goto_functionst;
class namespacet;

void transform_loops(
  goto_functionst &goto_functions,
  const namespacet &ns);

#endif // CPROVER_GOTO_INSTRUMENT_LOOP_TRANSFORMS_H
