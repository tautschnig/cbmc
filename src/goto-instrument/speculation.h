/*******************************************************************\

Module: Simulate semantics of speculating CPU

Author: Michael Tautschnig

Date: June 2018

\*******************************************************************/

/// \file
/// Program transformation to simulation speculation

#ifndef CPROVER_GOTO_INSTRUMENT_SPECULATION_H
#define CPROVER_GOTO_INSTRUMENT_SPECULATION_H

class goto_modelt;

void instrument_speculation(goto_modelt &);

// clang-format off
#define OPT_INSTRUMENT_SPECULATION \
  "(speculation)"

#define HELP_INSTRUMENT_SPECULATION \
  " --speculation                transform the program to simulate behavior\n" \
  "                              of a speculating CPU\n" \

#define OPT_SPECULATION \
  "(speculation)"

// clang-format on
#define HELP_SPECULATION \
  " --speculation                simulate behavior of a speculating CPU\n" \


#endif // CPROVER_GOTO_INSTRUMENT_SPECULATION_H
