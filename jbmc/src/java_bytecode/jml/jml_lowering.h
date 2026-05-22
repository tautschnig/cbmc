/*******************************************************************\

Module: JML-to-GOTO Lowering

Author: Kiro (AI agent)

\*******************************************************************/

/// \file
/// Lower JML specifications into GOTO-level contracts. Produces
/// the same code_with_contract_typet representation that JVerify
/// contracts use, so all downstream machinery (DFCC, symex,
/// property checking) works unchanged.

#ifndef CPROVER_JAVA_BYTECODE_JML_JML_LOWERING_H
#define CPROVER_JAVA_BYTECODE_JML_JML_LOWERING_H

#include "jml_spec_loader.h"

#include <goto-programs/goto_model.h>

#include <set>

/// Lower JML contracts from the contract map into the GOTO model.
///
/// For each method with JML specs:
///   - requires clauses → c_requires + body-level ASSUME at entry
///   - ensures clauses → c_ensures + body-level ASSERT at returns
///   - assignable clauses → c_assigns
///
/// Returns the set of annotated function identifiers (for modular
/// substitution).
///
/// \param goto_model: The GOTO model to modify.
/// \param contracts: The parsed JML contract map.
/// \return Set of function identifiers that have JML contracts.
std::set<irep_idt> lower_jml_contracts(
  goto_modelt &goto_model,
  const jml_contract_mapt &contracts);

#endif // CPROVER_JAVA_BYTECODE_JML_JML_LOWERING_H
