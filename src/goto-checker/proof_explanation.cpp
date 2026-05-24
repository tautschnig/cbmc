/*******************************************************************\

Module: Word-level Proof Explanation

Author: CBMC Contributors

\*******************************************************************/

/// \file
/// Word-level Proof Explanation

#include "proof_explanation.h"

#include <util/find_symbols.h>
#include <util/format_expr.h>
#include <util/namespace.h>

#include <goto-symex/ssa_step.h>
#include <goto-symex/symex_target_equation.h>
#include <solvers/conflict_provider.h>
#include <solvers/stack_decision_procedure.h>

#include <map>
#include <sstream>

/// Return a human-readable string for an SSA step type
std::string step_type_string(const SSA_stept &step)
{
  if(step.is_assignment())
    return "assignment";
  if(step.is_assume())
    return "assumption";
  if(step.is_assert())
    return "assertion";
  if(step.is_constraint())
    return "constraint";
  if(step.is_goto())
    return "goto";
  if(step.is_decl())
    return "declaration";
  if(step.is_function_call())
    return "function call";
  if(step.is_function_return())
    return "function return";
  return "other";
}

/// Build a description string for an SSA step
std::string step_description(const SSA_stept &step, const namespacet &ns)
{
  std::ostringstream oss;

  if(step.is_assignment())
  {
    oss << format(step.ssa_lhs) << " = " << format(step.ssa_rhs);
  }
  else if(step.is_assume())
  {
    oss << format(step.cond_expr);
  }
  else if(step.is_assert())
  {
    if(!step.comment.empty())
      oss << step.comment;
    else
      oss << format(step.cond_expr);
  }
  else if(step.is_constraint())
  {
    oss << format(step.cond_expr);
  }
  else if(step.is_goto())
  {
    oss << "branch on " << format(step.cond_expr);
  }
  else if(step.is_function_call())
  {
    oss << step.called_function << "(...)";
  }
  else if(step.is_decl())
  {
    oss << format(step.ssa_lhs);
  }

  return oss.str();
}

/// Check whether a source location has useful information
static bool has_useful_source_location(const source_locationt &loc)
{
  return !loc.get_file().empty() || !loc.get_line().empty();
}

/// Check whether an SSA step should be included in the proof explanation.
/// This contains the common filtering logic used by both the basic and
/// core-based approaches.
bool is_relevant_proof_step(const SSA_stept &step)
{
  // Skip steps that were sliced away
  if(step.ignore)
    return false;

  // We are interested in assignments, assumptions, and constraints
  // that form the proof. Skip purely structural steps.
  if(
    !step.is_assignment() && !step.is_assume() && !step.is_constraint() &&
    !step.is_assert() && !step.is_goto() && !step.is_decl())
  {
    return false;
  }

  // Skip assertions (they are what we are proving, not
  // part of the explanation)
  if(step.is_assert())
    return false;

  // Skip declarations without meaningful content
  if(step.is_decl())
    return false;

  // Skip gotos -- they add noise for the initial explanation
  if(step.is_goto())
    return false;

  const source_locationt &loc = step.source.pc->source_location();
  if(!has_useful_source_location(loc))
    return false;

  // Skip internal built-in initialization steps
  if(
    id2string(loc.get_file()).find("<built-in-") != std::string::npos ||
    id2string(loc.get_file()).find("<builtin-") != std::string::npos)
  {
    return false;
  }

  // Skip internal SSA assignments (return values, etc.)
  if(step.is_assignment())
  {
    const std::string lhs_str = id2string(step.ssa_lhs.get_identifier());
    if(
      lhs_str.find("goto_symex::") != std::string::npos ||
      lhs_str.find("return'") != std::string::npos)
    {
      return false;
    }
  }

  return true;
}

step_kindt classify_step(const SSA_stept &step)
{
  // Inspect the source-location comment that JBMC's contract-
  // lowering passes attach. See java_bytecode_contracts.cpp and
  // jml_lowering.cpp for the canonical comment strings.
  const auto &loc = step.source.pc->source_location();
  const auto &comment = loc.get_comment();
  if(!comment.empty())
  {
    const std::string c = id2string(comment);
    if(
      c == "JML requires" || c == "JVerify precondition" ||
      c == "JVerify assume")
    {
      return step_kindt::PRECONDITION;
    }
    if(
      c == "JML ensures" || c == "JVerify postcondition" ||
      c == "JVerify postcondition (lambda, unresolved)" || c == "JVerify check")
    {
      return step_kindt::POSTCONDITION;
    }
    if(c == "JML loop invariant" || c == "JVerify loop invariant")
    {
      return step_kindt::LOOP_INVARIANT;
    }
    if(
      c == "JML decreases (non-negative)" ||
      c == "JVerify decreases (non-negativity)")
    {
      return step_kindt::DECREASES;
    }
    if(c == "JML \\old capture")
    {
      return step_kindt::OLD_CAPTURE;
    }
  }
  // property_class-based fallback for contract-derived ASSERTs
  // that DFCC may have rewritten beyond the original comment.
  const auto &property_class = loc.get_property_class();
  if(!property_class.empty())
  {
    const std::string p = id2string(property_class);
    if(p == "precondition")
      return step_kindt::PRECONDITION;
    if(p == "postcondition")
      return step_kindt::POSTCONDITION;
    if(p == "loop_invariant")
      return step_kindt::LOOP_INVARIANT;
  }

  // DFCC's contracts machinery uses synthetic write-set helpers
  // and locals named __CPROVER_contracts_*. Filter these out so
  // they don't dominate the static-coverage warnings.
  if(step.is_assignment())
  {
    const std::string lhs_id = id2string(step.ssa_lhs.get_object_name());
    if(
      lhs_id.find("__CPROVER_contracts_") != std::string::npos ||
      lhs_id.find("__contract_write_set") != std::string::npos ||
      lhs_id.find("__requires_write_set") != std::string::npos ||
      lhs_id.find("__ensures_write_set") != std::string::npos ||
      lhs_id.find("__ptr_pred_ctx") != std::string::npos ||
      lhs_id.find("__no_alloc_dealloc") != std::string::npos ||
      lhs_id.find("__write_set_to_check") != std::string::npos)
    {
      return step_kindt::CONTRACTS_INTERNAL;
    }
  }

  // No contract provenance — fall back to the structural kind.
  if(step.is_assignment())
    return step_kindt::REGULAR_ASSIGNMENT;
  if(step.is_assume())
    return step_kindt::REGULAR_ASSUMPTION;
  if(step.is_assert())
    return step_kindt::REGULAR_ASSERTION;
  return step_kindt::OTHER;
}

std::vector<static_coverage_warningt>
classify_uncovered(const std::vector<proof_explanation_stept> &explanation)
{
  std::vector<static_coverage_warningt> warnings;
  for(const auto &step : explanation)
  {
    if(step.in_core)
      continue;
    // Filter DFCC synthetic steps — they produce noise without
    // surfacing real specification gaps.
    if(step.step_kind == step_kindt::CONTRACTS_INTERNAL)
      continue;
    static_coverage_warningt w;
    w.loc = step.source_location;
    w.description = step.description;
    switch(step.step_kind)
    {
    case step_kindt::PRECONDITION:
      w.kind = static_coverage_warningt::kindt::UNNECESSARY_PRECONDITION;
      break;
    case step_kindt::POSTCONDITION:
      w.kind = static_coverage_warningt::kindt::VACUOUS_POSTCONDITION;
      break;
    case step_kindt::LOOP_INVARIANT:
      w.kind = static_coverage_warningt::kindt::UNNECESSARY_INVARIANT;
      break;
    case step_kindt::REGULAR_ASSUMPTION:
      w.kind = static_coverage_warningt::kindt::UNNECESSARY_ASSUMPTION;
      break;
    case step_kindt::REGULAR_ASSIGNMENT:
      w.kind = static_coverage_warningt::kindt::UNCONSTRAINED_CODE;
      break;
    case step_kindt::DECREASES:
    case step_kindt::OLD_CAPTURE:
    case step_kindt::CONTRACTS_INTERNAL:
    case step_kindt::REGULAR_ASSERTION:
    case step_kindt::OTHER:
      // No actionable warning for these kinds; their unused-ness
      // is rarely meaningful for the user (DECREASES/OLD_CAPTURE
      // are bookkeeping; CONTRACTS_INTERNAL is DFCC noise; an
      // assertion that's not in the core means a different
      // assertion proved it).
      continue;
    }
    warnings.push_back(std::move(w));
  }
  return warnings;
}

std::vector<proof_explanation_stept> get_proof_explanation(
  const symex_target_equationt &equation,
  const namespacet &ns)
{
  std::vector<proof_explanation_stept> result;

  for(const auto &step : equation.SSA_steps)
  {
    if(!is_relevant_proof_step(step))
      continue;

    proof_explanation_stept explanation_step;
    explanation_step.source_location = step.source.pc->source_location();
    explanation_step.step_type = step_type_string(step);
    explanation_step.description = step_description(step, ns);
    explanation_step.in_core = true;
    explanation_step.step_kind = classify_step(step);

    result.push_back(std::move(explanation_step));
  }

  return result;
}

std::vector<proof_explanation_stept> get_proof_explanation_with_core(
  const symex_target_equationt &equation,
  stack_decision_proceduret &solver,
  const namespacet &ns)
{
  // Collect candidate steps and their guard handles
  struct candidate_infot
  {
    const SSA_stept *step;
    exprt guard_handle;
  };

  std::vector<candidate_infot> candidates;
  for(const auto &step : equation.SSA_steps)
  {
    if(!is_relevant_proof_step(step))
      continue;

    candidates.push_back({&step, step.guard_handle});
  }

  if(candidates.empty())
    return {};

  // Try to use assumption-based conflict analysis.
  // We need the solver to support the conflict_providert interface.
  auto *conflict_provider = dynamic_cast<conflict_providert *>(&solver);

  // Collect non-constant guard handles as assumptions
  std::vector<exprt> assumptions;
  // Track which candidate index maps to which assumption index
  std::vector<std::size_t> assumption_to_candidate;

  if(conflict_provider != nullptr)
  {
    for(std::size_t i = 0; i < candidates.size(); ++i)
    {
      const exprt &gh = candidates[i].guard_handle;
      // Only non-constant handles can be checked for conflict.
      // Constant-true guards are always active, constant-false
      // guards mean the step is unreachable.
      if(!gh.is_constant())
      {
        assumptions.push_back(gh);
        assumption_to_candidate.push_back(i);
      }
    }
  }

  // Perform assumption-based conflict analysis if possible
  std::vector<bool> in_conflict(candidates.size(), true);

  if(conflict_provider != nullptr && !assumptions.empty())
  {
    solver.push(assumptions);

    auto result = solver();

    if(result == decision_proceduret::resultt::D_UNSATISFIABLE)
    {
      // Mark all candidates as not-in-core initially
      for(std::size_t j = 0; j < in_conflict.size(); ++j)
        in_conflict[j] = false;

      // Check each assumption for conflict membership
      for(std::size_t i = 0; i < assumptions.size(); ++i)
      {
        if(conflict_provider->is_in_conflict(assumptions[i]))
          in_conflict[assumption_to_candidate[i]] = true;
      }

      // Steps with constant-true guards are always active;
      // keep them in the core
      for(std::size_t i = 0; i < candidates.size(); ++i)
      {
        if(candidates[i].guard_handle.is_true())
          in_conflict[i] = true;
      }
    }
    // If the result is not UNSAT (unexpected), we fall back
    // to marking all steps as in-core (the default).

    solver.pop();
  }

  // Build the result
  std::vector<proof_explanation_stept> result;
  for(std::size_t i = 0; i < candidates.size(); ++i)
  {
    const SSA_stept &step = *candidates[i].step;

    proof_explanation_stept explanation_step;
    explanation_step.source_location = step.source.pc->source_location();
    explanation_step.step_type = step_type_string(step);
    explanation_step.description = step_description(step, ns);
    explanation_step.in_core = in_conflict[i];
    explanation_step.step_kind = classify_step(step);

    result.push_back(std::move(explanation_step));
  }

  return result;
}

/// Strip SSA level suffixes from a variable name.
/// SSA identifiers have the form "name!N@M#L0#L1#L2";
/// this function returns the portion before the first '#'.
static std::string strip_ssa_suffix(const std::string &id)
{
  auto pos = id.find('#');
  if(pos != std::string::npos)
    return id.substr(0, pos);
  return id;
}

/// Strip scope encoding from a variable name.
/// CBMC scope-encoded names look like "function::N::name!N@M";
/// this function extracts just the local name part after the
/// last "::" and before any '!' suffix.
static std::string clean_display_name(const std::string &name)
{
  std::string result = name;

  // Remove the scope prefix (e.g., "main::1::" -> "")
  auto pos = result.rfind("::");
  if(pos != std::string::npos)
    result = result.substr(pos + 2);

  // Remove renaming suffixes (e.g., "!0@1")
  pos = result.find('!');
  if(pos != std::string::npos)
    result = result.substr(0, pos);

  return result;
}

std::vector<proof_invariantt> extract_proof_invariants(
  const std::vector<proof_explanation_stept> &explanation,
  const symex_target_equationt &equation,
  const namespacet &ns)
{
  // We need to correlate explanation steps back to SSA steps
  // to extract the underlying expressions. We collect the
  // relevant SSA steps in the same order as the explanation.
  std::vector<const SSA_stept *> relevant_steps;
  for(const auto &step : equation.SSA_steps)
  {
    if(is_relevant_proof_step(step))
      relevant_steps.push_back(&step);
  }

  // Map from stripped variable name to invariant entry.
  // Use an ordered map so output is deterministic.
  std::map<std::string, proof_invariantt> invariant_map;

  // Process each explanation step that is in the core
  for(std::size_t i = 0; i < explanation.size() && i < relevant_steps.size();
      ++i)
  {
    if(!explanation[i].in_core)
      continue;

    const SSA_stept &ssa_step = *relevant_steps[i];

    if(ssa_step.is_assignment())
    {
      // For assignments, the key variable is the LHS
      const std::string full_id = id2string(ssa_step.ssa_lhs.get_identifier());
      const std::string var_key = strip_ssa_suffix(full_id);
      const std::string display = clean_display_name(var_key);

      if(display.empty())
        continue;

      auto &inv = invariant_map[var_key];
      inv.variable = var_key;
      inv.display_name = display;
      inv.constraints.push_back(explanation[i].description);
    }
    else if(ssa_step.is_assume() || ssa_step.is_constraint())
    {
      // For assumptions/constraints, find all symbol variables
      // referenced in the condition expression
      std::set<symbol_exprt> symbols;
      find_symbols(ssa_step.cond_expr, symbols);

      if(symbols.empty())
        continue;

      for(const auto &sym : symbols)
      {
        const std::string full_id = id2string(sym.get_identifier());

        // Skip internal symbols
        if(
          full_id.find("goto_symex::") != std::string::npos ||
          full_id.find("return'") != std::string::npos)
        {
          continue;
        }

        const std::string var_key = strip_ssa_suffix(full_id);
        const std::string display = clean_display_name(var_key);

        if(display.empty())
          continue;

        auto &inv = invariant_map[var_key];
        inv.variable = var_key;
        inv.display_name = display;
        inv.constraints.push_back(explanation[i].description);
      }
    }
  }

  // Convert map to vector
  std::vector<proof_invariantt> result;
  result.reserve(invariant_map.size());
  for(auto &pair : invariant_map)
    result.push_back(std::move(pair.second));

  return result;
}
