/*******************************************************************\

Module: Replace all type symbols identifier by canical names

Author: Michael Tautschnig

\*******************************************************************/

#ifndef CPROVER_LINKING_CANONICALIZE_TYPE_SYMBOLS_H
#define CPROVER_LINKING_CANONICALIZE_TYPE_SYMBOLS_H

class message_handlert;
class symbol_tablet;

void canonicalize_type_symbols(
  symbol_tablet &symbol_table,
  message_handlert &message_handler);

#endif
