# intersection, difference and symmetric_difference of sets of strings.
u = {"a", "b", "c", "d"}
assert u.intersection({"a", "e"}, ["a", "b"]) == {"a"}
assert u.difference({"a"}, ["b"]) == {"c", "d"}
assert {"a", "b"}.symmetric_difference({"b", "c"}) == {"a", "c"}
c = u.copy()
c.clear()
assert len(c) == 0 and len(u) == 4
