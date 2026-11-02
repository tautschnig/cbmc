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
///   and into every later update of such an entry (as any thread may
///   write those values at any time);
/// - until a run records no value that the summary does not contain, or
///   every lookup in the run already had the values that the other threads
///   wrote to the entry (see needs_rerun).
/// Then every read of a shared object in the last run has, in its value
/// set, every value any thread may have written to it.
///
/// The initial values of objects allocated after the first spawn are not
/// recorded when they are assigned: no other thread can read them until
/// the object is published, i.e., a pointer to it is written to a shared
/// object, or a thread is spawned. Then the current values of the
/// object's entries (and of the objects reachable from them) are recorded.
/// Otherwise, the zero values of the fields of every new object (null
/// pointers) would be in the value sets of all its readers, whose
/// dereferences would then consider invalid objects.
///
/// The summary refers to objects by name, so their names must agree across
/// runs (see goto_symext::fresh_object_symbol).

#ifndef CPROVER_GOTO_SYMEX_SHARED_VALUE_SUMMARY_H
#define CPROVER_GOTO_SYMEX_SHARED_VALUE_SUMMARY_H

#include <pointer-analysis/value_set.h>

#include <functional>
#include <map>
#include <set>
#include <unordered_map>

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
  /// summary into them, and record updates (merging the summary into
  /// them) from now on.
  void spawn(value_sett &value_set);

  /// Object \p object is allocated: its initial value is assigned until
  /// end_allocation.
  void begin_allocation(const irep_idt &object);

  /// The initial value of the object of begin_allocation is assigned.
  void end_allocation()
  {
    initialising.clear();
  }

  /// Symbolic execution continues with thread \p thread_number.
  void set_thread(std::size_t thread_number)
  {
    thread = thread_number;
  }

  /// Whether a thread has been spawned in the current run.
  bool concurrent() const
  {
    return active;
  }

  /// Add the values recorded by the current run to the summary, and tell
  /// whether the run needs to be repeated: the summary grew, and a lookup
  /// of some entry in this run did not have a value that another thread
  /// wrote to it (see read). \p value_set is used to merge object maps.
  bool needs_rerun(const value_sett &value_set);

  /// Whether \p identifier (an L1 identifier) belongs to a shared object.
  bool shared(const irep_idt &identifier) const;

  void read(
    const value_sett &value_set,
    const irep_idt &identifier,
    const irep_idt &index,
    const value_sett::entryt *entry) override;

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
  /// For each entry of a shared object looked up since the first spawn of
  /// the current run, and each thread that looked it up, the values that
  /// all its lookups had (none if it was absent at some lookup).
  std::unordered_map<irep_idt, std::map<std::size_t, value_sett::object_mapt>>
    seen;
  /// For each entry of a shared object, the values recorded for it in the
  /// current run, by the thread that wrote them.
  std::unordered_map<irep_idt, std::map<std::size_t, value_sett::object_mapt>>
    written;
  /// The thread that symbolic execution executes.
  std::size_t thread = 0;

  /// Whether every lookup of an entry in the current run had the values
  /// that the other threads wrote to it (see needs_rerun).
  bool lookups_had_values(const value_sett &value_set) const;
  /// The objects allocated after the first spawn of the current run, with
  /// the indices of their entries.
  std::unordered_map<irep_idt, std::set<irep_idt>> young;
  /// The object whose initial value is being assigned, if any.
  irep_idt initialising;

  /// The object that the entries with \p identifier belong to.
  static irep_idt object_of(const irep_idt &identifier);

  /// Record the current values of the entries of the objects allocated
  /// after the first spawn that \p values points to, and of those
  /// reachable from them.
  void
  publish(const value_sett &value_set, const value_sett::object_mapt &values);

  void record(
    const value_sett &value_set,
    const irep_idt &index,
    const value_sett::entryt &entry,
    const value_sett::object_mapt &values);
};

#endif // CPROVER_GOTO_SYMEX_SHARED_VALUE_SUMMARY_H
