# Equal set elements are dropped (PLR 6.2.6) in set comprehensions.
c = {y for y in ["a", "a"]}
assert len(c) == 1
d = {y + "1" for y in ["a", "b", "a"]}
assert len(d) == 2
assert "b1" in d
assert d == {"a1", "b1"}
