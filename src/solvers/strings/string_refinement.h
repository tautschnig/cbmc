/*******************************************************************\

Module: String support via creating string constraints and progressively
        instantiating the universal constraints as needed.
        The procedure is described in the PASS paper at HVC'13:
        "PASS: String Solving with Parameterized Array and Interval Automaton"
        by Guodong Li and Indradeep Ghosh

Author: Alberto Griggio, alberto.griggio@gmail.com

\*******************************************************************/

/// \file
/// String support via creating string constraints and progressively
///   instantiating the universal constraints as needed. The procedure is
///   described in the PASS paper at HVC'13: "PASS: String Solving with
///   Parameterized Array and Interval Automaton" by Guodong Li and Indradeep
///   Ghosh
///
/// What its results, its ERROR verdicts and its approximation warnings mean
/// is described in \ref string-solver-results.

#ifndef CPROVER_SOLVERS_REFINEMENT_STRING_REFINEMENT_H
#define CPROVER_SOLVERS_REFINEMENT_STRING_REFINEMENT_H

#include <util/union_find_replace.h>

#include <solvers/refinement/bv_refinement.h>

#include "string_constraint_generator.h"
#include "string_dependencies.h"
#include "string_length_ties.h"
#include "string_refinement_util.h"

#define OPT_STRING_REFINEMENT                                                  \
  "(no-refine-strings)"                                                        \
  "(string-printable)"                                                         \
  "(string-input-value):"                                                      \
  "(string-non-empty)"                                                         \
  "(max-nondet-string-length):"

#define HELP_STRING_REFINEMENT                                                 \
  " {y--no-refine-strings} \t turn off string refinement\n"                    \
  " {y--string-printable} \t restrict to printable strings and characters\n"   \
  " {y--string-non-empty} \t restrict to non-empty strings (experimental)\n"   \
  " {y--string-input-value} {ust} \t "                                         \
  "restrict non-null strings to a fixed value {ust}; the switch can be used "  \
  "multiple times to give several strings\n"                                   \
  " {y--max-nondet-string-length} {un} \t "                                    \
  "bound the length of nondet (e.g. input) strings. Default is " +             \
    std::to_string(MAX_CONCRETE_STRING_SIZE - 1) +                             \
    "; note that setting the value higher than this does not work "            \
    "with {y--trace} or {y--validate-trace}.\n"

// The integration of the string solver into CBMC is incomplete. Therefore,
// it is not turned on by default and not all options are available.
#define OPT_STRING_REFINEMENT_CBMC                                             \
  "(refine-strings)"                                                           \
  "(string-printable)"

#define HELP_STRING_REFINEMENT_CBMC                                            \
  " {y--refine-strings} \t use string refinement (experimental)\n"             \
  " {y--string-printable} \t restrict to printable strings (experimental)\n"

#define DEFAULT_MAX_NB_REFINEMENT std::numeric_limits<size_t>::max()

class string_refinementt final : public bv_refinementt
{
private:
  struct configt
  {
    std::size_t refinement_bound = 0;
    bool use_counter_example = true;
  };

public:
  /// string_refinementt constructor arguments
  struct infot : public bv_refinementt::infot, public configt
  {
  };

  explicit string_refinementt(const infot &);

  std::string decision_procedure_text() const override
  {
    return "string refinement loop with " + prop.solver_text();
  }

  exprt get(const exprt &expr) const override;
  void set_to(const exprt &expr, bool value) override;
  /// Keep the guard of definitions by string builtin functions, whose
  /// constraints are then conditional on it.
  void set_definition(const exprt &guard, const exprt &definition) override;

protected:
  decision_proceduret::resultt dec_solve(const exprt &) override;

  // Override to capture boolean-typed string function applications
  // (e.g. cprover_string_is_prefix_func) before they are converted
  // to fresh SAT literals. The multi-assertion path in
  // symex_target_equationt::convert_assertions calls handle() on
  // each assertion, which triggers convert() and bit-blasts the
  // function_application into a fresh literal; by the time set_to()
  // is invoked on the composed goal, the original function_application
  // is gone and add_node never sees it. Tracking them here and
  // feeding them to add_node in dec_solve() ensures axioms are
  // generated regardless of whether the goal was a single assertion
  // or a multi-assertion disjunction.
  literalt convert_rest(const exprt &expr) override;
  bvt convert_function_application(
    const function_application_exprt &expr) override;

private:
  // Base class
  typedef bv_refinementt supert;

  string_refinementt(const infot &, bool);

  const configt config_;
  std::size_t loop_bound_;
  string_constraint_generatort generator;

  // Simple constraints that have been given to the solver
  std::set<exprt> seen_instances;

  string_axiomst axioms;

  // Unquantified lemmas that have newly been added
  std::vector<exprt> current_constraints;

  // See the definition in the PASS article
  // Warning: this is indexed by array_expressions and not string expressions

  index_set_pairt index_sets;
  /// Index terms deferred by filter_index_set, by array.
  std::map<exprt, std::set<exprt>> pending_indices;
  void filter_index_set();
  /// Results of substitute_array_access, shared by all lemmas.
  std::unordered_map<exprt, exprt, irep_hash> substitution_memo;
  /// Makes equal subterms of lemmas share one node, bottom up: children
  /// are merged first, so that looking a node up compares children by
  /// address, and each node of a shared term is visited once.
  class lemma_mergert
  {
  public:
    void operator()(exprt &expr)
    {
      irept result = merged(expr);
      visited.clear();
      expr = static_cast<exprt &>(result);
    }

  private:
    std::unordered_set<irept, irep_hash> store;
    /// Results by node within one lemma, with the original kept alive so
    /// that its address is not reused.
    std::unordered_map<const void *, std::pair<irept, irept>> visited;
    irept merged(const irept &irep);
  };
  lemma_mergert lemma_merger;
  void add_length_definitions();
  union_find_replacet symbol_resolve;

  std::vector<exprt> equations;
  /// The guard of each of \ref equations, true for most.
  std::vector<exprt> equation_guards;

  // String-specific function_applications captured via convert_*
  // before bit-blasting strips them. Feeds into add_node in
  // dec_solve(). Cleared at the end of each dec_solve call.
  // Each entry holds the original application plus the fresh literal
  // variable that convert_rest / convert_function_application
  // returned for it; we tie that literal to the axiom-generated
  // return_code so the assertion's converted form sees the same
  // truth value the string axioms assign.
  struct recorded_string_application_entryt
  {
    function_application_exprt application;
    // For boolean results: the literal converted-to. bvt for bitvector
    // results. Only one of these is populated.
    std::optional<literalt> bool_lit;
    std::optional<bvt> bv;
  };
  std::vector<recorded_string_application_entryt> recorded_string_applications;

  string_dependenciest dependencies;
  /// Sets the lengths of the arrays the array pool makes for pointers.
  string_length_tiest length_ties;

  /// Definitions of the names of accesses to merged strings, made by
  /// rewriting array accesses, that add_lemma has yet to add.
  std::vector<exprt> access_definitions;

  void add_lemma(const exprt &lemma, bool simplify_lemma = true);
  /// Add a counter-example lemma of check_axioms, rewriting its array
  /// accesses as for instances (see substitute_array_access): it may access
  /// merged strings, whose if-expressions over arrays are not well typed.
  void add_counter_example(const exprt &lemma);
  /// Whether the results of the conversions of floating-point values to
  /// strings that the axioms leave open (see
  /// string_constraint_generatort::float_conversions) are right in the
  /// current model. Otherwise, add lemmas giving the result for the value
  /// of each wrong one.
  bool check_float_conversions();
};

exprt substitute_array_lists(exprt expr, std::size_t string_max_length);

// Declaration required for unit-test:
union_find_replacet string_identifiers_resolution_from_equations(
  const std::vector<equal_exprt> &equations,
  const namespacet &ns,
  messaget::mstreamt &stream);

// Declaration required for unit-test:
exprt substitute_array_access(
  exprt expr,
  symbol_generatort &symbol_generator,
  const bool left_propagate);

#endif
