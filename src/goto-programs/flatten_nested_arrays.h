/// \file
/// Flatten nested arrays: array(array(T, M), N) -> array(T, N*M)

#ifndef CPROVER_GOTO_PROGRAMS_FLATTEN_NESTED_ARRAYS_H
#define CPROVER_GOTO_PROGRAMS_FLATTEN_NESTED_ARRAYS_H

class goto_modelt;

void flatten_nested_arrays(goto_modelt &);

#endif
