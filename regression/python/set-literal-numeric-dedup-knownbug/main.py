# KNOWNBUG (container-literal cross-type numeric dedup cluster). A set LITERAL
# counts distinct elements with type-sensitive equality, so `{1, 1.0}` is modelled
# with two elements. Python uses numeric equality for set membership: 1 == 1.0
# (and both hash equal), so `{1, 1.0}` has ONE element. CPython: len == 1, so this
# `assert len({1, 1.0}) == 2` raises AssertionError; cbmc proves len == 2 (false
# proof). Desired: VERIFICATION FAILED. NB the INCREMENTAL paths are already sound
# (`s = {1}; s.add(1.0)` dedups; `frozenset([1, 1.0])` dedups) -- only the literal
# builder is affected. Fix: dedup set-literal elements by Python numeric equality
# (1 == 1.0 == True), matching the dict-subscript key handling.
assert len({1, 1.0}) == 2
