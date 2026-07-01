# KNOWNBUG: a chained assignment `a = b = <mutable>` evaluates the RHS ONCE and
# binds BOTH targets to the SAME object (PLR §7.2), so a mutation through one is
# visible through the other. The frontend binds each target to an independent
# copy, so `a.append(2)` is invisible to `b`. CPython: len(b) == 2. Desired:
# VERIFICATION FAILED.
a = b = [1]
a.append(2)
assert len(b) == 1
