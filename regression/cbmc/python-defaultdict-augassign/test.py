# Regression: `d[key] += rhs` where d is a dict-typed variable must
# not produce an L-value whose deepest else-branch is a struct
# literal. The frontend now decomposes the AugAssign into
# (1) a manual chained read with safe_zero default (no KeyError
# check) and (2) an iterator-based dict-store with append on
# missing keys, mirroring defaultdict's auto-insert semantics.
# Previously the assignment crashed with
# "l2_rename_rvalues case 'struct' not handled".

from collections import defaultdict
d: dict = defaultdict(int)
d["a"] += 1
d["b"] += 1
