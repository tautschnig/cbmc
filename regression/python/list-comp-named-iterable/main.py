# List comprehension over a named list literal — previously
# only supported when the iterable was an inline List node.


# Direct List-literal iterable: already worked.
a = [x * 2 for x in [1, 2, 3]]
assert len(a) == 3
assert a[0] == 2
assert a[2] == 6

# Named list literal: previously rejected.
xs = [1, 2, 3, 4]
b = [x + 10 for x in xs]
assert len(b) == 4
assert b[0] == 11
assert b[3] == 14

# With filter.
c = [x for x in xs if x % 2 == 0]
assert len(c) == 2
assert c[0] == 2
assert c[1] == 4

# Transformation + filter.
d = [y * 3 for y in [1, 2, 3, 4, 5] if y > 2]
assert len(d) == 3
assert d[0] == 9
assert d[2] == 15
