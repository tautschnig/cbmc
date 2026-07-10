# PLR §6.3.2: concatenating lists with DIFFERENT element types. The general
# concat read the right operand's data using the LEFT operand's element type,
# producing a definite WRONG value for e.g. list-of-tuples + list-of-int -- which
# let `!=` be FALSE-PROVED (mutation-oracle). Non-constant different-type concat
# is now a sound nondet list. CPython: r == [(1,3),(2,4),5], so `r != ...` is
# False -> AssertionError (verification FAILS, not a false proof).
r = list(zip([1, 2], [3, 4])) + [5]
assert r != [(1, 3), (2, 4), 5]
