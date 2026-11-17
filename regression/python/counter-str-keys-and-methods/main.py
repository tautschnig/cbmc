from collections import Counter

w = Counter(["a", "b", "a"])
assert w["a"] == 2 and w["z"] == 0
assert "b" in w and "q" not in w
w["b"] += 3
assert w["b"] == 4
w.update(["a"])
assert w["a"] == 3
assert w.total() == 7
n = Counter([1, 1, 2])
assert sorted(n.elements()) == [1, 1, 2]
assert 2 in n and 3 not in n
