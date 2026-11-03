/*******************************************************************\

Module: String solver

Author: Michael Tautschnig

\*******************************************************************/

/// \file
/// Lengths of the arrays that the array pool makes for pointers.
///
/// `array_poolt::find(p, l)` stands for the string with content pointer `p`
/// and length `l`. For a pointer that no association binds to an array, the
/// pool makes an array whose length is a fresh symbol. If every find of `p`
/// gives a length of the same value `l`, the string at `p` has length `l` in
/// every execution, so the pool length of its array can be set to `l`.
/// Lengths whose values may differ (the same buffer read with different
/// lengths) leave the length unconstrained.

#ifndef CPROVER_SOLVERS_STRINGS_STRING_LENGTH_TIES_H
#define CPROVER_SOLVERS_STRINGS_STRING_LENGTH_TIES_H

#include <util/std_expr.h>
#include <util/union_find_replace.h>

#include <unordered_map>
#include <vector>

class array_poolt;

/// Decides which arrays made for pointers have a single length, and gives
/// the equations that set their pool lengths.
class string_length_tiest
{
public:
  /// Record the integer copies `x == y` (where `y` is a symbol, a member or
  /// a nondeterministic symbol) and merges `x == (c ? a : b)` among
  /// \p equations. Equal lengths are found by syntax after these
  /// rewritings, so \p equations must hold unconditionally.
  void add_definitions(const std::vector<exprt> &equations);

  /// The equations `length(array) == l` for the arrays of
  /// \p pool.arrays_made_for_pointers() whose pointers have a single length
  /// `l` up to the recorded definitions, and that were not tied by an
  /// earlier call.
  std::vector<exprt> new_ties(const array_poolt &pool);

  /// Whether a pointer that an earlier call tied was found with a length
  /// that may differ from the one it was tied to. The tie may then exclude
  /// executions, so results must not be trusted.
  bool conflict() const
  {
    return has_conflict;
  }

private:
  /// \p length after the recorded copies and merges.
  exprt canonical(exprt length) const;

  union_find_replacet copies;
  std::unordered_map<exprt, exprt, irep_hash> merges;
  /// The length each pointer was tied to.
  std::unordered_map<exprt, exprt, irep_hash> tied;
  bool has_conflict = false;
};

#endif // CPROVER_SOLVERS_STRINGS_STRING_LENGTH_TIES_H
