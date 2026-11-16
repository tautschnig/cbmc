/*******************************************************************\

Module: Python front-end: exception propagation at call sites

\*******************************************************************/

/// \file
/// After goto conversion, a call that may raise is followed by a jump to
/// the end of its Python statement when an exception is active (PLR 8.4).

#ifndef CPROVER_PYTHON_PYTHON_EXCEPTION_INSTRUMENTATION_H
#define CPROVER_PYTHON_PYTHON_EXCEPTION_INSTRUMENTATION_H

#include <util/irep.h>

class goto_modelt;

/// Source-location key (a comment, not compared) linking a call that may
/// raise to the SKIP that ends its Python statement.
inline const irep_idt ID_python_exc_stmt{"#python_exc_stmt"};

/// goto-convert lifts calls out of expressions, so within one Python
/// statement code after a raising call (the rest of the expression, the
/// store) would still run. For every function call tagged by the front-end
/// (ID_python_exc_stmt = N), insert `IF __exception_active THEN GOTO end`
/// right after it, where `end` is the SKIP carrying the same tag. From
/// there the statement-level sequencing (guards, handler dispatch, the
/// function's exception exit) takes over.
void instrument_python_call_exceptions(goto_modelt &goto_model);

#endif // CPROVER_PYTHON_PYTHON_EXCEPTION_INSTRUMENTATION_H
