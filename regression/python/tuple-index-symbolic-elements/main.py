# PLR §6.10: tuple.index over a tuple with SYMBOLIC (non-constant) numeric
# elements. The symbolic path required all-CONSTANT elements, so `t = (b, a)`
# then t.index(v) missed the ValueError for an absent v -- a false proof found by
# the mutation-oracle. The match is now built over the element expressions.
b = 3
a = 1
t = (b, a)
assert t.index(3) == 0
assert t.index(1) == 1
