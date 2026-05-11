# PLR builtins: sorted(iterable, /, *, key=None, reverse=False).
# We handle reverse=; key= is not yet supported. Constant
# literal lists sort at parse time (fast path); non-literal
# lists use the bubble-sort symex path with the comparison
# direction switched on reverse=True.


# Ascending default (literal list parse-time path).
a = sorted([3, 1, 2])
assert a[0] == 1
assert a[1] == 2
assert a[2] == 3

# Named list literal — list_literals lookup.
xs = [5, 2, 8, 1, 3]
b = sorted(xs)
assert b[0] == 1
assert b[1] == 2
assert b[2] == 3
assert b[3] == 5
assert b[4] == 8

# reverse=True descending.
c = sorted([3, 1, 2], reverse=True)
assert c[0] == 3
assert c[1] == 2
assert c[2] == 1

# Already sorted input.
d = sorted([1, 2, 3])
assert d[0] == 1 and d[2] == 3

# Empty
e = sorted([])
assert len(e) == 0

# Single element
f = sorted([42])
assert len(f) == 1
assert f[0] == 42
