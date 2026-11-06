#!/usr/bin/env python3
"""Summarise a CBMC --write-solver-stats-to file: which source lines and
which SSA steps produce the clauses.  Usage: solver_hardness_summary.py FILE

The file is large (1.4 GB for the FreeBSD cleanup proof at ring size 4) and
is loaded whole; bound the memory of this script like any other heavy run."""
import collections
import json
import sys

entries = json.load(open(sys.argv[1]))
by_loc = collections.Counter()
steps = []
total = 0
for e in entries:
    clauses = e.get('SAT_hardness', {}).get('Clauses', 0)
    if not clauses:
        continue
    total += clauses
    src = e.get('Source', {})
    loc = '%s:%s %s' % (src.get('file', '?').split('/')[-1],
                        src.get('line', '?'), src.get('function', ''))
    by_loc[loc] += clauses
    steps.append((clauses, loc, e.get('SSA_expr', '')[:160]))

print('total clauses attributed: %d (%d entries)' % (total, len(entries)))
print('--- by source location')
for loc, clauses in by_loc.most_common(15):
    print('%11d %5.1f%%  %s' % (clauses, 100.0 * clauses / total, loc))
print('--- largest single SSA steps')
for clauses, loc, ssa in sorted(steps, reverse=True)[:8]:
    print('%11d  %s\n             %s' % (clauses, loc, ssa))
