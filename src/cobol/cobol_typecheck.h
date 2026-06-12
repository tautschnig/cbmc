/*******************************************************************\

Module: COBOL Parsing and Type Checking

Author: Kiro

\*******************************************************************/

/// \file
/// Parses the COBOL token stream into a symbol table: DATA DIVISION items
/// become symbols, and each PROGRAM-ID's PROCEDURE DIVISION becomes a single
/// goto-function whose body is a \ref codet tree (lowered to GOTO by CBMC's
/// goto_convert).

#ifndef CPROVER_COBOL_COBOL_TYPECHECK_H
#define CPROVER_COBOL_COBOL_TYPECHECK_H

#include "cobol_scanner.h"

#include <string>
#include <vector>

class symbol_table_baset;
class message_handlert;

/// Parse and typecheck a COBOL token stream into \p symbol_table.
/// \param tokens: token stream from cobol_scan
/// \param symbol_table: destination symbol table
/// \param module: module name (source file)
/// \param message_handler: for diagnostics
/// \return true on error
bool cobol_typecheck(
  const std::vector<cobol_tokent> &tokens,
  symbol_table_baset &symbol_table,
  const std::string &module,
  message_handlert &message_handler);

#endif // CPROVER_COBOL_COBOL_TYPECHECK_H
