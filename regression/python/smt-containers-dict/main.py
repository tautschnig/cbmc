# P2 of doc/python-frontend-unbounded-containers-plan.md
# (--python-smt-containers): dicts carry INFINITE keys/values arrays.
# The keys array IS the insertion order (PLR 3.7) — preserved by
# construction, including through mutation; KeyError vs None-valued
# keys stay distinguished (PLR 6.4.6); lookups scan a bounded prefix
# and FAIL CLOSED past it (twin tests).
d = {"x": 1, "y": None, "z": 3}
assert d["x"] == 1
assert d.get("y") is None
assert "y" in d
assert not ("missing" in d)
assert len(d) == 3

# insertion order, incl. after append + delete
ks = list(d.keys())
assert ks[0] == "x"
assert ks[1] == "y"
assert ks[2] == "z"
d["w"] = 4
ks2 = list(d.keys())
assert ks2[3] == "w"
del d["y"]
assert len(d) == 3
assert not ("y" in d)

# mutation ops
d2 = {"a": 1, "b": 2}
d2.update({"b": 20, "c": 30})
assert d2["a"] == 1
assert d2["b"] == 20
assert d2["c"] == 30
assert d2.setdefault("a", 99) == 1
assert d2.pop("b") == 20
assert len(d2) == 2

# iteration
total = 0
for k in d2:
    total += 1
assert total == 2

# comprehension with int keys
squares = {n: n * n for n in range(4)}
assert squares[3] == 9
