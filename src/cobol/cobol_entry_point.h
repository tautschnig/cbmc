/*******************************************************************\

Module: COBOL Language Entry Point

Author: Kiro

\*******************************************************************/

/// \file
/// Builds __CPROVER_initialize and __CPROVER__start for a COBOL program.

#ifndef CPROVER_COBOL_COBOL_ENTRY_POINT_H
#define CPROVER_COBOL_COBOL_ENTRY_POINT_H

class symbol_table_baset;
class message_handlert;

/// Create the initialize and start functions for the COBOL program in
/// \p symbol_table.
/// \param symbol_table: symbol table containing the program function
/// \param message_handler: for diagnostics
/// \return true on error
bool cobol_entry_point(
  symbol_table_baset &symbol_table,
  message_handlert &message_handler);

#endif // CPROVER_COBOL_COBOL_ENTRY_POINT_H
