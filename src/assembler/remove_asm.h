/*******************************************************************\

Module: Remove 'asm' statements by compiling them into suitable
        standard goto program instructions

Author: Daniel Kroening

Date:   December 2014

\*******************************************************************/

/// \file
/// Remove 'asm' statements by compiling them into suitable standard goto
/// program instructions.
/// \details
/// Inline assembly statements in the source program (in either gcc- or
/// msc-style) are represented by instructions of kind `OTHER` with a `code`
/// member of type `code_asmt` in the goto program.
///
/// For example, the gcc inline assembly statement below
///
/// ```
/// int input = 1;
/// int result;
///
/// asm volatile (
///   "mov %1, %0; add $1, %0"
///   : "=r" (result)
///   : "r" (input)
///   : "cc");
/// ```
///
/// would be represented by a `code_asmt` statement with `op0()` being the
/// string literal "mov %1, %0; add $1, %0" (as a `string_constantt` expression)
/// and `op1()`, `op2()`, and `op3()`, respectively, containing the output list,
/// input list, and clobbered registers.
///
/// The `remove_asm()` function replaces the inline assembly statements in a
/// given goto program with appropriate (sequences of) non-assembly goto program
/// instructions.
///
/// It first parses the assembly instructions string (via `assembler_parsert`)
/// and inspects the input/output/clobber lists. Then, it generates a sequence
/// of goto program instructions that either directly implement the assembly
/// instructions, or call a suitable library function (see
/// `library/x86_assembler.c`).
///
/// Statements are lowered soundly: each is either interpreted exactly, when
/// every instruction in it is understood, or approximated conservatively,
/// when it is not, by havocking everything it may change (and a warning is
/// printed).  See \ref inline-assembly (doc/architectural/inline-assembly.md)
/// for the design and for how to extend the instruction and architecture
/// coverage.

#ifndef CPROVER_ASSEMBLER_REMOVE_ASM_H
#define CPROVER_ASSEMBLER_REMOVE_ASM_H

class goto_functionst;
class goto_modelt;
class message_handlert;
class symbol_tablet;

void remove_asm(goto_functionst &, symbol_tablet &, message_handlert &);

void remove_asm(goto_modelt &, message_handlert &);

/// returns true iff the given goto functions use asm instructions
bool has_asm(const goto_functionst &);

/// returns true iff the given goto model uses asm instructions
bool has_asm(const goto_modelt &);

#endif // CPROVER_ASSEMBLER_REMOVE_ASM_H
