# The string equality u == "ab" is evaluated only when the earlier
# conjuncts hold. Its definition is conditional on the guard of that
# branch, and the formula slicer removed the guard's definition, which
# left the equality unconstrained.
k = nondet_int()
t = nondet_str()
u = nondet_str()
if k == 0 and t == "b" and u == "ab":
    assert len(u) == 2
    assert u.find(t) == 1
