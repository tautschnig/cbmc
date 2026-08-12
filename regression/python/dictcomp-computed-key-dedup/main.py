# last value wins per clash (PLR 6.2.7)
d = {x % 2: x for x in [0, 1, 2]}
assert d[0] == 2
assert d[1] == 1
# cross-type numeric coalescing through computed keys
e = {x * 1.0: x for x in [1, 2]}
f = {1: 10, 1.0: 20}
assert len(f) == 1 and f[1] == 20
# computed STRING keys (concat folding)
g = {"a" + "b": 1, "ab": 2}
assert len(g) == 1 and g["ab"] == 2
