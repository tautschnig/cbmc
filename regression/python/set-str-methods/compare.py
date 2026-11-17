# issubset, issuperset and isdisjoint raised AttributeError.
u = {"a", "b", "c", "d"}
assert {"a"}.issubset(u) and u.issuperset({"b", "c"})
assert u.isdisjoint({"e"}) and not u.isdisjoint({"a", "e"})
