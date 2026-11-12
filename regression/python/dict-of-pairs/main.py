# dict() of an iterable of pairs (PLib stdtypes): later values of a key
# replace earlier ones, keeping the first position.
d = dict([(1, 2), (3, 4)])
assert d[1] == 2 and d[3] == 4 and len(d) == 2
pairs = [("a", 1), ("b", 2), ("a", 3)]
e = dict(pairs)
assert e["a"] == 3 and len(e) == 2
z = dict(zip(["x", "y"], [1, 2]))
assert z["y"] == 2
x = nondet_int()
ps = [(x, 1), (2, 5)]
ps.append((x, 7))
f = dict(ps)
if x == 2:
    assert len(f) == 1 and f[2] == 7
else:
    assert len(f) == 2 and f[x] == 7
c = dict(e)
c["z"] = 9
assert len(e) == 2 and len(c) == 3
