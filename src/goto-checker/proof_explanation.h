/*******************************************************************\

Module: Word-level Proof Explanation

Author: CBMC Contributors

\*******************************************************************/

/// \file
/// Word-level Proof Explanation

#ifndef CPROVER_GOTO_CHECKER_PROOF_EXPLANATION_H
#define CPROVER_GOTO_CHECKER_PROOF_EXPLANATION_H

#include <util/irep.h>
#include <util/source_location.h>

#include <string>
#include <type_traits>
#include <vector>

class namespacet;
class stack_decision_proceduret;
class symex_target_equationt;
struct SSA_stept;

/// A single step in a proof explanation, representing a program step
/// that contributes to proving a property.
/// Source-artifact classification of an SSA step (Tomb & Joshi
/// FMCAD 2025 static coverage). Distinguishes contract-derived
/// steps (precondition, postcondition, loop invariant, etc.) from
/// regular code so that `classify_uncovered()` can produce
/// per-element-type warnings instead of a unified in-core /
/// not-in-core listing.
enum class step_kindt
{
  /// Contract precondition (ASSUME at entry of a verified method).
  PRECONDITION,
  /// Contract postcondition (ASSERT at exit of a verified method).
  POSTCONDITION,
  /// Loop invariant (assertion side, on entry / exit / preservation).
  LOOP_INVARIANT,
  /// Termination-measure non-negativity (`JVerify.decreases` /
  /// `//@ decreases`).
  DECREASES,
  /// Synthetic DECL+ASSIGN at function entry that captures a
  /// pre-state value for `\\old` lookups.
  OLD_CAPTURE,
  /// DFCC-generated write-set machinery
  /// (`__CPROVER_contracts_write_set_*` etc.). Filtered out of
  /// static-coverage warnings to avoid false-positive noise.
  CONTRACTS_INTERNAL,
  /// Regular code-level assignment (no contract provenance).
  REGULAR_ASSIGNMENT,
  /// Regular code-level assumption (e.g. `__CPROVER_assume`).
  REGULAR_ASSUMPTION,
  /// Regular code-level assertion (e.g. `__CPROVER_assert`).
  REGULAR_ASSERTION,
  /// Anything not classified above.
  OTHER,
};

struct proof_explanation_stept
{
  /// Source location of the contributing step
  source_locationt source_location;

  /// Type of the contributing step (e.g., "assignment", "assumption")
  std::string step_type;

  /// Human-readable description of why this step contributes
  std::string description;

  /// Whether this step is in the unsat core (true by default
  /// for backward compatibility with the basic approach)
  bool in_core = true;

  /// Source-artifact classification (defaults to OTHER for
  /// callers that don't run the classifier).
  step_kindt step_kind = step_kindt::OTHER;
};

/// Classify an SSA step by its source-location comment / property
/// class. Reads the metadata that JBMC's contract-lowering passes
/// (`java_bytecode_contracts.cpp`, `jml_lowering.cpp`) and DFCC
/// already attach. Returns OTHER when no recognised provenance is
/// present.
step_kindt classify_step(const SSA_stept &step);

/// A static-coverage warning produced when a contract-derived SSA
/// step is *not* in the unsat core: i.e., the contract artifact
/// was unnecessary, vacuous, or otherwise irrelevant to the proof.
struct static_coverage_warningt
{
  enum class kindt
  {
    /// Precondition assumption was not used by any property's
    /// proof (the contract over-constrains the caller).
    UNNECESSARY_PRECONDITION,
    /// Postcondition was proved without engaging any non-trivial
    /// behaviour (the contract is vacuous).
    VACUOUS_POSTCONDITION,
    /// Loop invariant was unused (vacuously preserved or not
    /// referenced by any proof obligation).
    UNNECESSARY_INVARIANT,
    /// Code-level assumption was unnecessary.
    UNNECESSARY_ASSUMPTION,
    /// Assignment had no observable effect on any property.
    UNCONSTRAINED_CODE,
  };
  kindt kind;
  source_locationt loc;
  std::string description;
};

/// Produce per-element static-coverage warnings from a
/// proof-explanation. Each warning corresponds to a contract or
/// code element that did NOT contribute to the unsat core, with
/// element-type-aware classification.
///
/// This is the doc's "form (3)" explaining-proofs deliverable
/// (Tomb & Joshi FMCAD 2025), built on top of PR #8927's
/// proof-explanation infrastructure. CONTRACTS_INTERNAL steps
/// (DFCC write-set machinery) are filtered to avoid noise.
std::vector<static_coverage_warningt>
classify_uncovered(const std::vector<proof_explanation_stept> &explanation);

/// Extract a word-level proof explanation from an UNSAT result.
/// After the solver returns UNSATISFIABLE, this function iterates
/// over the SSA steps in the equation and identifies which steps
/// contribute to the proof. Steps that are not sliced away and
/// have non-trivial guards or conditions are collected.
/// \param equation: the SSA equation after solving
/// \param ns: the namespace for expression pretty-printing
/// \return a vector of proof explanation steps
std::vector<proof_explanation_stept> get_proof_explanation(
  const symex_target_equationt &equation,
  const namespacet &ns);

// Helpers used by both proof_explanation.cpp and
// goto_symex_property_decider.cpp
std::string step_type_string(const SSA_stept &step);
std::string step_description(const SSA_stept &step, const namespacet &ns);
bool is_relevant_proof_step(const SSA_stept &step);

/// Extract a word-level proof explanation with unsat core information.
/// After the solver returns UNSATISFIABLE, this function iterates
/// over the SSA steps in the equation and identifies which steps
/// contribute to the proof. Additionally, it uses the solver's
/// assumption-based conflict analysis to determine which steps are
/// truly in the unsat core. Steps whose guard handles are in the
/// conflict are marked with in_core=true.
/// \param equation: the SSA equation after solving
/// \param solver: the decision procedure (must support push/pop)
/// \param ns: the namespace for expression pretty-printing
/// \return a vector of proof explanation steps with core annotations
std::vector<proof_explanation_stept> get_proof_explanation_with_core(
  const symex_target_equationt &equation,
  stack_decision_proceduret &solver,
  const namespacet &ns);

/// A word-level invariant extracted from the proof explanation.
/// Groups related constraints by the variable they constrain.
struct proof_invariantt
{
  /// The variable this invariant is about (original SSA name)
  irep_idt variable;

  /// Source-level name of the variable (without SSA suffixes)
  std::string display_name;

  /// The human-readable expressions constraining this variable
  std::vector<std::string> constraints;
};

/// Extract word-level invariants from a proof explanation.
/// Groups core steps by the variables they constrain and
/// produces one invariant summary per variable.
/// \param explanation: the proof explanation steps (with core info)
/// \param equation: the SSA equation used during analysis
/// \param ns: the namespace for expression pretty-printing
/// \return a vector of proof invariants, one per variable
std::vector<proof_invariantt> extract_proof_invariants(
  const std::vector<proof_explanation_stept> &explanation,
  const symex_target_equationt &equation,
  const namespacet &ns);

/// Type trait to detect whether a checker type T
/// has a get_proof_explanation() method.
template <typename T, typename = void>
struct has_get_proof_explanationt : std::false_type
{
};

template <typename T>
struct has_get_proof_explanationt<
  T,
  std::void_t<decltype(std::declval<T>().get_proof_explanation())>>
  : std::true_type
{
};

/// Type trait to detect whether a checker type T
/// has a get_proof_invariants() method.
template <typename T, typename = void>
struct has_get_proof_invariantst : std::false_type
{
};

template <typename T>
struct has_get_proof_invariantst<
  T,
  std::void_t<decltype(std::declval<T>().get_proof_invariants())>>
  : std::true_type
{
};

/// Type trait for get_per_property_proof_explanations().
template <typename T, typename = void>
struct has_get_per_property_proof_explanationst : std::false_type
{
};

template <typename T>
struct has_get_per_property_proof_explanationst<
  T,
  std::void_t<
    decltype(std::declval<T>().get_per_property_proof_explanations())>>
  : std::true_type
{
};

#endif // CPROVER_GOTO_CHECKER_PROOF_EXPLANATION_H
