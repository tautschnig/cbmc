# The assertion fails in CPython.
# set() (ints) union a set of strings gave the empty set.
s = set()
t = {"a", "b"}
assert len(s.union(t)) == 0, "mixed_union"
