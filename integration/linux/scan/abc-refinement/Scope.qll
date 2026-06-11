/**
 * Per-subsystem scoping for the threat finders.
 *
 * A finder gated with `inScope(f)` runs DB-wide by default, but can be
 * restricted to a single leaf subsystem (path prefix) at query-run time
 * WITHOUT recompiling, by supplying the external predicate from a CSV:
 *
 *   codeql query run ... --external=scopePrefix=/tmp/scope.csv
 *
 * where scope.csv contains one row, e.g. `drivers/usb/`.  With no
 * `--external`, the relation is empty, `scopeActive()` is false, and
 * `inScope` is universally true -- so the unscoped behaviour is preserved
 * exactly.  Gating the flagged function as the FIRST where-conjunct prunes
 * the expensive per-candidate work (taint reachability, dominance) to the
 * leaf, which is what makes whole-`drivers/` DBs tractable per leaf.
 */

import cpp

/** One row per scope path prefix (kernel-relative, e.g. `drivers/usb/`).
 *  Populated at run time via `--external=scopePrefix=<file.csv>`; empty
 *  when not supplied. */
external predicate scopePrefix(string p);

/** Holds when a scope prefix has been supplied. */
predicate scopeActive() { exists(string p | scopePrefix(p)) }

/** Holds if `e` is in the active scope -- universally true when no scope
 *  prefix is supplied (unscoped, whole-DB behaviour). */
predicate inScope(Element e) {
  scopeActive()
  implies
  exists(string p | scopePrefix(p) |
    e.getLocation().getFile().getRelativePath().matches(p + "%")
  )
}
