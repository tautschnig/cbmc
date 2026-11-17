# update added nothing; union returned a set without the new elements.
u = {"a", "b"}
u.update({"c"}, ["d"])
assert len(u) == 4 and "c" in u and "d" in u
w = u.union({"e"})
assert len(w) == 5 and "e" in w and "e" not in u
