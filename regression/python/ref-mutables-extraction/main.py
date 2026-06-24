# Reference-semantics spike (--python-ref-mutables): a nested mutable list
# element is stored by reference (heap-allocated, aliased by pointer), so
# extraction (`r = c[i]`), reassignment, membership, value reads and nested
# composition all follow CPython reference semantics PRECISELY -- no by-value
# copy, no sound-but-imprecise havoc guard. See
# doc/python-frontend-reference-semantics-spike.md.

# Extraction-then-mutate: r aliases the shared object.
g = [[1]]
r = g[0]
r.append(5)
assert len(g[0]) == 2
assert 5 in g[0]

# Multi-instance: each construction allocates a DISTINCT object (no aliasing
# across separate calls).
def mk():
    return [[]]
a = mk()
b = mk()
a[0].append(5)
assert len(b[0]) == 0

# Reorder/reassign: `g2[0] = [99]` rebinds the slot to a NEW object; the prior
# alias keeps the OLD object (slot-aliasing got this wrong).
g2 = [[1]]
r2 = g2[0]
g2[0] = [99]
r2.append(5)
assert g2[0] == [99]
assert len(r2) == 2

# Value reads through the reference, including 3-deep composition.
g3 = [[99]]
assert g3[0][0] == 99
g4 = [[[1]]]
g4[0][0].append(5)
assert len(g4[0][0]) == 2
