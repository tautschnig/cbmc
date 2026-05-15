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

#include <set>

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
/// with GOTO-level assertions and assumptions, AND populates each
/// annotated function symbol's type with `code_with_contract_typet`
/// clauses (`c_requires` / `c_ensures` / `c_assigns`) so CBMC's
/// modular contracts machinery can substitute calls when invoked.
///
/// \param goto_model: the model to transform (modified in place)
/// \return set of fully-qualified function identifiers that carry at
///   least one JVerify primitive — these are the candidates for
///   `code_contractst::replace_calls` in modular mode.
std::set<irep_idt> lower_jverify_contracts(goto_modelt &goto_model);

/// F12: Modular contract substitution. After
/// `lower_jverify_contracts` has populated contract clauses on each
/// annotated function symbol, walk every call to such a function and
/// substitute it with `assert(requires); havoc(assigns); assume
/// (ensures);`. The callee's body is left in place — it can be
/// verified independently with CBMC's `enforce_contracts`.
///
/// This is the gate that lifts JBMC verification from purely-inlining
/// (multiplicative cost) to modular (each annotated callee verified
/// once).
///
/// \param goto_model: model carrying contract clauses on annotated
///   function types.
/// \param annotated: set of function ids to substitute.
void apply_modular_contract_substitution(
  goto_modelt &goto_model,
  const std::set<irep_idt> &annotated);

#endif // CPROVER_JBMC_JAVA_BYTECODE_CONTRACTS_H
