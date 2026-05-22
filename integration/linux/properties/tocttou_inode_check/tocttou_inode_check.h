/// \file
/// tocttou_inode_check.h — property module for "time-of-check
/// vs time-of-use" race bugs on shared filesystem state.
///
/// ## Bug class
///
/// A thread reads a piece of shared state, decides based on
/// the read, and acts.  A concurrent thread can mutate the
/// state between the read and the act, breaking the
/// assumption the act was predicated on:
///
/// ```c
/// // Thread A:
/// if (inode->i_size > MAX) {
///     // ... possible interleave: another thread truncates ...
///     do_something_with(inode->i_size);  // now smaller than MAX
/// }
///
/// // Thread B (concurrent):
/// truncate(inode);  // changes i_size
/// ```
///
/// The fix is either to hold a lock across the check-and-act
/// (so they are atomic together), to re-validate at the act
/// site, or to act on the captured value rather than
/// re-reading the field.
///
/// Motivating CVE class: the TOCTOU subset of `race_or_toctoue`
/// (232 CVEs total, with concrete examples like
/// CVE-2026-43420 [ceph i_nlink underrun during async unlink]
/// and CVE-2026-43439 [cgroup race between task migration and
/// iteration]).
///
/// ## Abstraction
///
/// One integer ghost `__tic_state` representing the shared
/// state.  Three contracts:
///
///   * `tic_check`  — returns the current `__tic_state` value
///                    via __CPROVER_ensures.
///   * `tic_act_assuming` — requires the captured value matches
///                          the current state; assigns nothing.
///   * `tic_change` — concurrent mutator; assigns + ensures
///                    `__tic_state` to the new value.
///
/// CBMC's concurrent symex explores all interleavings of a
/// check-and-act thread with a mutator thread.  The bug shape
/// is detected when CBMC finds an interleaving where the
/// mutator runs between check and act, breaking the
/// `tic_act_assuming` precondition.
///
/// ## What this module covers
///
/// * **Direct-call concurrent harness** with vuln/fix shapes.
/// * **Cocci prefilter** for "if (cond) action(field)" patterns
///   where field is a shared state element.
///
/// ## What this module does NOT cover
///
/// * **Per-file synthesis** — concurrent reasoning doesn't
///   fit per-file directly.

#ifndef INTEGRATION_LINUX_PROPERTIES_TOCTTOU_INODE_CHECK_H
#define INTEGRATION_LINUX_PROPERTIES_TOCTTOU_INODE_CHECK_H

extern unsigned int __tic_state;

void tic_init(unsigned int initial);
unsigned int tic_check(void);
void tic_act_assuming(unsigned int checked);
void tic_change(unsigned int new_state);

#endif
