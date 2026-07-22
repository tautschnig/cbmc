# PLR §6.2.1/§6.5.3: in-place mutation of an extracted container alias
# flows through FOUR channels -- mutator methods (append/...), augmented
# assign (`v += [x]`), subscript store (`v[i] = x`), and `del v[i]`.
# Invalidation was keyed on mutator METHOD NAMES only, so the other three
# bypassed it entirely: `v = d[1]; v += [5]; assert len(d[1]) == 1`
# proved the STALE length -- a false-proof class, closed by the
# statement-level channel scan (handle_alias_mutation_channels) feeding
# the same write-through-or-havoc core. The stale reads below must FAIL;
# the true values are asserted in the CORE precision tests.
d = {1: [9]}
v = d[1]
v += [5]
assert len(d[1]) == 1  # stale: CPython len is 2

d2 = {1: [9]}
w = d2[1]
w[0] = 7
assert d2[1][0] == 9  # stale: CPython reads 7

d3 = {1: [9, 8]}
x = d3[1]
del x[0]
assert len(d3[1]) == 2  # stale: CPython len is 1

g = [[9]]
r = g[0]
r += [5]
assert len(g[0]) == 1  # stale: CPython len is 2
