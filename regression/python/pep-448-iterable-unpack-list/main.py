# PEP 448: additional unpacking generalizations.
# Iterable unpacking in list literals:
#     [*a, 3, 4] → list with a's elements then 3, 4.
# Our frontend handles list literals with known length
# at conversion time; non-literal iterables are
# over-approximated (sound — the resulting list is a
# subset of what runtime would produce).

a = [1, 2]
b = [*a, 3, 4]
assert len(b) == 4
assert b[0] == 1
assert b[1] == 2
assert b[2] == 3
assert b[3] == 4

# Starred at different positions
c = [10, *a, 20]
assert len(c) == 4
assert c[0] == 10
assert c[1] == 1
assert c[2] == 2
assert c[3] == 20

# Multiple starred
d = [*a, *[5, 6]]
assert len(d) == 4
