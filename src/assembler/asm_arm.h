/*******************************************************************\

Module: Inline assembly: arm instruction semantics

Author: Kiro

\*******************************************************************/

/// \file
/// Instruction semantics for arm

#ifndef CPROVER_ASSEMBLER_ASM_ARM_H
#define CPROVER_ASSEMBLER_ASM_ARM_H

#include <memory>

class asm_semanticst;

std::unique_ptr<asm_semanticst> make_asm_arm_semantics();

#endif // CPROVER_ASSEMBLER_ASM_ARM_H
