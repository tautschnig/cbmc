/*******************************************************************\

Module: Inline assembly: conservative approximation

Author: Kiro

\*******************************************************************/

/// \file
/// The conservative approximation of inline assembly statements that are not
/// understood: everything the statement may change is havocked.

#ifndef CPROVER_ASSEMBLER_ASM_APPROXIMATION_H
#define CPROVER_ASSEMBLER_ASM_APPROXIMATION_H

#include <util/irep.h>

#include <string>
#include <vector>

class code_asmt;
class exprt;
class goto_programt;
class symbol_table_baset;

/// Append to \p dest code that havocs everything the GCC-style statement
/// \p code may change: its outputs, and, if it clobbers "memory", the objects
/// its pointer operands point to and all objects of static lifetime.
/// \return a description of what is havocked, for diagnostics
std::string approximate_gcc_asm(
  const code_asmt &code,
  const symbol_table_baset &symbol_table,
  goto_programt &dest);

/// Append to \p dest code that havocs the C objects \p lvalues, the objects
/// they point to, and all objects of static lifetime, for statements without
/// an operand interface (MSVC-style `__asm` blocks).
/// \return a description of what is havocked, for diagnostics
std::string approximate_asm_block(
  const code_asmt &code,
  const std::vector<exprt> &lvalues,
  const symbol_table_baset &symbol_table,
  goto_programt &dest);

#endif // CPROVER_ASSEMBLER_ASM_APPROXIMATION_H
