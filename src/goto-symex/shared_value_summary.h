/*******************************************************************\

Module: Symbolic Execution: value sets of shared objects

Author: Michael Tautschnig

\*******************************************************************/

/// \file
/// Sound value sets for objects shared between threads.
///
/// Symbolic execution runs threads one after another, but the value set
/// (used to resolve dereferences) is that of a single sequential
/// execution. A pointer that a thread executed later writes to a shared
/// object is therefore missing from the value set of a thread that reads
/// the object earlier in symex order, and dereferences there miss the
/// object it points to (GitHub issue #305). The partial-order encoding of
/// concurrency only constrains the values read, not which objects a read
/// pointer may point to.
///
/// This class makes the value sets of shared objects (objects that are not
/// thread-local, and dirty locals) a sound over-approximation, without
/// changing dereferencing or the memory model:
/// - from the first thread spawn on, it records the values assigned to the
///   entries of shared objects (and, at each spawn, the values the entries
///   have then);
/// - symbolic execution is repeated, with the values recorded so far (the
///   summary) merged into the value sets of shared objects at each spawn
///   and into every later update of such an entry (so that updates are
///   weak, as another thread may still read an earlier value);
/// - until a run records no value that the summary does not contain.
/// Then every read of a shared object in the last run has, in its value
/// set, every value any thread may have written to it.

#ifndef CPROVER_GOTO_SYMEX_SHARED_VALUE_SUMMARY_H
#define CPROVER_GOTO_SYMEX_SHARED_VALUE_SUMMARY_H

#include <pointer-analysis/value_set.h>

#include <functional>
#include <map>

class shared_value_summaryt : public value_sett::update_observert
{
public:
  /// Whether the value-set entries with \p identifier (an L1 identifier)
  /// belong to an object shared between threads.
  using is_sharedt = std::function<bool(const irep_idt &identifier)>;

  explicit shared_value_summaryt(is_sharedt is_shared)
    : is_shared(std::move(is_shared))
  {
  }

  /// Start a run of symbolic execution: the values recorded by earlier runs
  /// are the summary, and recording starts afresh at the first spawn.
  void begin_run();

  /// A thread is spawned with value set \p value_set (of the spawning
  /// thread): record the current values of shared entries, merge the
  /// summary into them, and record (and weaken) updates from now on.
  void spawn(value_sett &value_set);

  /// Whether a thread has been spawned in the current run.
  bool concurrent() const
  {
    return active;
  }

  /// Whether the current run recorded values that the summary does not
  /// contain (then the run needs to be repeated), after adding them to the
  /// summary. \p value_set is used to merge object maps.
  bool grew(const value_sett &value_set);

  /// Whether \p identifier (an L1 identifier) belongs to a shared object.
  bool shared(const irep_idt &identifier) const;

  const value_sett::object_mapt *updated(
    const value_sett &value_set,
    const irep_idt &index,
    const value_sett::entryt &entry,
    const value_sett::object_mapt &values,
    bool add_to_sets) override;

  /// The number of runs started.
  std::size_t runs() const
  {
    return run_count;
  }

private:
  is_sharedt is_shared;
  bool active = false;
  std::size_t run_count = 0;
  using entriest = std::map<irep_idt, value_sett::entryt>;
  entriest summary;
  entriest recorded;

  void record(
    const value_sett &value_set,
    const irep_idt &index,
    const value_sett::entryt &entry,
    const value_sett::object_mapt &values);
};

#endif // CPROVER_GOTO_SYMEX_SHARED_VALUE_SUMMARY_H
