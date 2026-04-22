/// \file
/// CDCL(T) array theory propagator for CaDiCaL.
///
/// Watches equality literals created by the array theory and propagates
/// array axioms when equalities are assigned:
/// - Store axiom: (i == j) ∧ a = store(b, j, v) → a[i] = v
/// - Congruence: (i == j) → a[i] = a[j]
/// - Array equality: (a == b) → a[i] = b[i]

#ifndef CPROVER_SOLVERS_FLATTENING_ARRAY_PROPAGATOR_H
#define CPROVER_SOLVERS_FLATTENING_ARRAY_PROPAGATOR_H

#include <cadical.hpp>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class array_propagatort : public CaDiCaL::ExternalPropagator
{
public:
  array_propagatort()
  {
    is_lazy = false; // eager propagation
  }

  // === Setup (called before solving) ===

  /// Register an implication: when watch_lit becomes true,
  /// propagate implied_lit. The reason clause is: ¬watch ∨ implied.
  void add_implication(int watch_lit, int implied_lit)
  {
    implications[watch_lit].push_back(implied_lit);
    all_watched.insert(abs(watch_lit));
    all_watched.insert(abs(implied_lit));
  }

  /// Register a clause to add when watch_lit becomes true.
  /// The clause must contain watch_lit (for the reason).
  void add_triggered_clause(int watch_lit, std::vector<int> clause)
  {
    triggered_clauses[watch_lit].push_back(std::move(clause));
    all_watched.insert(abs(watch_lit));
  }

  const std::unordered_set<int> &watched_vars() const
  {
    return all_watched;
  }

  /// Register an Ackermann clause: (-idx_eq ∨ elem_eq).
  /// Checked on complete models via cb_check_found_model.
  void add_ackermann_clause(int idx_eq_dimacs, int elem_eq_dimacs)
  {
    ackermann_clauses.push_back({idx_eq_dimacs, elem_eq_dimacs});
    all_watched.insert(abs(idx_eq_dimacs));
    all_watched.insert(abs(elem_eq_dimacs));
  }

  // === CaDiCaL callbacks ===

  void notify_assignment(const std::vector<int> &lits) override
  {
    for(int lit : lits)
    {
      assigned.insert(lit);
      assigned.erase(-lit);

      // Check if this literal triggers any implications
      auto it = implications.find(lit);
      if(it != implications.end())
      {
        for(int implied : it->second)
        {
          // Only propagate if not already assigned
          if(!assigned.count(implied) && !assigned.count(-implied))
          {
            pending_propagations.push_back({lit, implied});
          }
        }
      }

      // Check triggered clauses
      auto ct = triggered_clauses.find(lit);
      if(ct != triggered_clauses.end())
      {
        for(const auto &clause : ct->second)
          pending_ext_clauses.push_back(clause);
      }
    }
  }

  void notify_new_decision_level() override
  {
    trail_sizes.push_back(trail.size());
  }

  void notify_backtrack(size_t new_level) override
  {
    // Undo assignments back to new_level
    while(trail_sizes.size() > new_level)
    {
      size_t old_size = trail_sizes.back();
      trail_sizes.pop_back();
      while(trail.size() > old_size)
      {
        assigned.erase(trail.back());
        trail.pop_back();
      }
    }
    // Clear pending propagations (they may be invalid after backtrack)
    pending_propagations.clear();
  }

  int cb_propagate() override
  {
    if(!pending_propagations.empty())
    {
      auto [reason, implied] = pending_propagations.back();
      pending_propagations.pop_back();
      current_reason = reason;
      trail.push_back(implied);
      assigned.insert(implied);
      return implied;
    }
    return 0;
  }

  int cb_add_reason_clause_lit(int propagated_lit) override
  {
    // Reason clause: ¬watch ∨ propagated_lit
    // Return literals one at a time, terminated by 0
    if(reason_phase == 0)
    {
      reason_phase = 1;
      return -current_reason; // ¬watch
    }
    if(reason_phase == 1)
    {
      reason_phase = 2;
      return propagated_lit;
    }
    reason_phase = 0;
    return 0; // end of clause
  }

  bool cb_check_found_model(const std::vector<int> &model) override
  {
    // Check Ackermann clauses against the model
    std::unordered_set<int> model_set(model.begin(), model.end());
    for(const auto &[idx_eq, elem_eq] : ackermann_clauses)
    {
      if(elem_eq != 0)
      {
        if(model_set.count(idx_eq) && model_set.count(-elem_eq))
          pending_ext_clauses.push_back({-idx_eq, elem_eq});
      }
      // Guard-only entries (elem_eq=0): checked by refinement loop
    }
    return pending_ext_clauses.empty();
  }

  bool cb_has_external_clause(bool &is_forgettable) override
  {
    is_forgettable = false;
    return !pending_ext_clauses.empty();
  }

  int cb_add_external_clause_lit() override
  {
    if(pending_ext_clauses.empty())
      return 0;
    auto &clause = pending_ext_clauses.front();
    if(ext_clause_idx < clause.size())
      return clause[ext_clause_idx++];
    ext_clause_idx = 0;
    pending_ext_clauses.erase(pending_ext_clauses.begin());
    return 0;
  }

private:
  // Ackermann clauses: (idx_eq, elem_eq) pairs
  std::vector<std::pair<int, int>> ackermann_clauses;

  // Watch → list of implied literals
  std::unordered_map<int, std::vector<int>> implications;
  // Watch → list of clauses to add
  std::unordered_map<int, std::vector<std::vector<int>>> triggered_clauses;
  std::unordered_set<int> all_watched;

  // Trail for backtracking
  std::unordered_set<int> assigned;
  std::vector<int> trail;
  std::vector<size_t> trail_sizes;

  // Pending propagations: (reason_lit, implied_lit)
  std::vector<std::pair<int, int>> pending_propagations;
  int current_reason = 0;
  int reason_phase = 0;

  // External clauses
  std::vector<std::vector<int>> pending_ext_clauses;
  size_t ext_clause_idx = 0;
};

#endif
