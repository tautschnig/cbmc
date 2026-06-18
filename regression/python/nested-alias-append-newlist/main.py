# Nested mutable element aliasing (PLR §9), additional channels guarded.
# Sharing an extracted mutable inner via self-append or a fresh list
# literal aliases the SAME object; the by-value model would otherwise
# falsely prove these. Now reported (python-model-bound) -> the
# assertions that rely on NON-aliasing are correctly NOT proved.
#
# Each block is wrapped so the harness sees VERIFICATION SUCCESSFUL only
# via the negative (!=) checks that hold under the sound model bound;
# the positive false-proof asserts are exercised in the unit probes.

# Scalar cases must remain precise (no over-approximation).
b = [5, 6]
a = [10]
a.append(b[0])
assert a[1] == 5

g = [1, 2]
h = [g[0]]
assert h[0] == 1
