# A set equals a set with the same elements in any order, a dict's keys()
# with the same keys, and no list.
s = {"a", "b"}
assert s == {"b", "a"} and not (s != {"b", "a"})
assert {"a"} != ["a"] and not ({"a"} == ["a"])
assert {"a": 1, "b": 2}.keys() == s
