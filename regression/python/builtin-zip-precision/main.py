# PLR §6.2.5: zip(a, b) yields (a[i], b[i]) up to the shorter input. The two-list
# case now materialises a precise list of tuples (was nondet).
r = list(zip([1, 2], [3, 4]))
assert r[0] == (1, 3)
assert r[1] == (2, 4)
assert len(list(zip([1, 2, 3], [4, 5]))) == 2  # stops at shorter
h = list(zip([1, 2], ["a", "b"]))  # heterogeneous
assert h[1] == (2, "b")
s = 0
for a, b in zip([10, 20], [1, 2]):
    s += a + b
assert s == 33
