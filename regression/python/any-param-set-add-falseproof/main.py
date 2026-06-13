def add_elem(s):
    s.add(5)


a = {1, 2}
add_elem(a)
# Soundness guard: CPython has 5 in a after the by-reference add, so this is
# FALSE and must NOT verify. Before sets were representable in the tagged union
# (SET tag) and shared by reference, this wrongly verified SUCCESSFUL.
assert 5 not in a
