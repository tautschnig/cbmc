def grow(x):
    x.append(99)


a = [1, 2]
grow(a)
# Soundness guard: CPython has len(a) == 3 after the by-reference append,
# so this is FALSE and must NOT verify. Before length-changing methods on
# Any-typed parameters propagated, this wrongly verified SUCCESSFUL.
assert len(a) == 2
