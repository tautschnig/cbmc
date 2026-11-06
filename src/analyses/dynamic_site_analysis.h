/*******************************************************************\

Module: Analysis of the values at dynamic sites

Author: Michael Tautschnig

\*******************************************************************/

/// \file
/// The strings that may reach each dynamic site (see
/// goto-programs/dynamic_sites.h), as candidates for the front-end to
/// convert the site for. See doc/ADR/dynamic_structure_resolution.md.
///
/// Values are sets of at most a given number of strings, or any string.
/// The analysis tracks symbols of string type, and symbols of struct type
/// with exactly one component of string type, which stands for the
/// symbol's string. Assignments outside __CPROVER_initialize are joined
/// flow-insensitively. Where a symbol is read, the value assigned in
/// __CPROVER_initialize, or for local variables any string, is joined in,
/// unless a must-assigned analysis shows that the program has assigned it
/// on every path. Sites the program cannot reach have no candidates.
///
/// The analysis need not be sound: each resolved site asserts that its
/// value is among the candidates.

#ifndef CPROVER_ANALYSES_DYNAMIC_SITE_ANALYSIS_H
#define CPROVER_ANALYSES_DYNAMIC_SITE_ANALYSIS_H

#include <goto-programs/dynamic_sites.h>

#include <cstddef>
#include <set>

class goto_modelt;

/// The number of dynamic sites, by what the analysis found for them.
struct dynamic_site_statisticst
{
  /// Sites with candidates.
  std::size_t finite = 0;
  /// Sites that the program cannot reach (with no candidates).
  std::size_t unreached = 0;
  /// Sites whose value may be any string.
  std::set<irep_idt> unresolved;
};

/// The candidates of the dynamic sites of \p goto_model: sets of at most
/// \p bound strings. A site whose value may be any string is absent.
/// Adds an empty body for DYNAMIC_SITE_FUNCTION to \p goto_model.
dynamic_resolutionst analyse_dynamic_sites(
  goto_modelt &goto_model,
  std::size_t bound,
  dynamic_site_statisticst &statistics);

#endif // CPROVER_ANALYSES_DYNAMIC_SITE_ANALYSIS_H
