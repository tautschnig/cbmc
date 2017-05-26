/*******************************************************************\

Module: Program Transformation

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

/// \file
/// Program Transformation

#ifndef CPROVER_GOTO_PROGRAMS_REMOVE_SKIP_H
#define CPROVER_GOTO_PROGRAMS_REMOVE_SKIP_H

#include "goto_functions.h"

bool is_skip(goto_programt::const_targett it);
void remove_skip(goto_programt &goto_program);
void remove_skip(goto_functionst &goto_functions);

#endif // CPROVER_GOTO_PROGRAMS_REMOVE_SKIP_H
