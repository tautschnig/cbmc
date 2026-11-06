/*******************************************************************\

Module: Go front-end: data races

Author: Michael Tautschnig

\*******************************************************************/

/// \file
/// Data races of Go programs (GoMem §Memory_model), by making conflicting
/// accesses that can be adjacent in some interleaving observable (see
/// doc/architectural/go-data-races.md).

#ifndef CPROVER_GO_GO_RACE_CHECK_H
#define CPROVER_GO_GO_RACE_CHECK_H

class goto_modelt;
class message_handlert;

/// Instrument the Go functions of \p goto_model (after function pointers
/// have been removed) with properties of class "data-race", if the program
/// may start a goroutine.
void go_race_check(goto_modelt &goto_model, message_handlert &message_handler);

#endif // CPROVER_GO_GO_RACE_CHECK_H
