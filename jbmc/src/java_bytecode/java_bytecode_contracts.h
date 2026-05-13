/*******************************************************************\

Module: Java Bytecode Contracts Support

Author: Michael Tautschnig (via Kiro)

Date: May 2026

\*******************************************************************/

/// \file
/// Recognize and lower JVerify-style and JML-style contract annotations
/// in Java bytecode to GOTO-level contract representations.
///
/// JVerify-style: static calls to org.strata.jverify.JVerify.precondition(),
///   postcondition(), invariant(), assume(), check()
///
/// Both styles lower to the same GOTO representation used by CBMC's
/// C contracts infrastructure (assertions and assumptions).

#ifndef CPROVER_JBMC_JAVA_BYTECODE_CONTRACTS_H
#define CPROVER_JBMC_JAVA_BYTECODE_CONTRACTS_H

#include <util/irep.h>

class goto_modelt;

/// Check if a method call is a JVerify contract method.
/// \param method_id: the fully qualified method identifier
/// \return true if this is a recognized JVerify contract method
bool is_jverify_contract_method(const irep_idt &method_id);

/// Describes what kind of contract a JVerify call represents.
enum class jverify_contract_kindt
{
  PRECONDITION,
  POSTCONDITION,
  INVARIANT,
  ASSUME,
  CHECK,
  DECREASES,
  NOT_A_CONTRACT
};

/// Classify a JVerify method call.
/// \param method_id: the fully qualified method identifier
/// \return the contract kind, or NOT_A_CONTRACT
jverify_contract_kindt classify_jverify_call(const irep_idt &method_id);

/// Post-pass over a GOTO model that replaces JVerify contract calls
/// with GOTO-level assertions and assumptions.
/// \param goto_model: the model to transform (modified in place)
void lower_jverify_contracts(goto_modelt &goto_model);

#endif // CPROVER_JBMC_JAVA_BYTECODE_CONTRACTS_H
