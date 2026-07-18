# PLR object identity, sibling extraction aliases: two extractions from
# the same container slot are the SAME runtime object -- a mutation
# through one is visible through the other, and through the direct slot.
# The by-value model havoced only the SOURCE container on mutation,
# leaving sibling copies (and copies mutated via the direct slot) stale:
# two probed FALSE PROOFS. Now every alias of the mutated source is
# havoced (nondet -- so the stale-value asserts below correctly FAIL).
d = {1: [1]}
v1 = d[1]
v2 = d[1]
v1.append(2)
assert len(v2) == 1  # CPython: 2 -> AssertionError; model: nondet -> FAILS
