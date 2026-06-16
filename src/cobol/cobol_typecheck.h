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
/// \param runtime_checks: emit implicit runtime-property checks (subscript /
///   reference-modification range); gated on CBMC's "bounds-check" option
/// \param div_checks: emit the division-by-zero check; gated on CBMC's
///   "div-by-zero-check" option
/// \return true on error
bool cobol_typecheck(
  const std::vector<cobol_tokent> &tokens,
  symbol_table_baset &symbol_table,
  const std::string &module,
  message_handlert &message_handler,
  bool runtime_checks,
  bool div_checks);

#endif // CPROVER_COBOL_COBOL_TYPECHECK_H
