# Tuple unpacking — PLR 7.2.1 ("The assignment statement"):
# the target list is bound after the expression list on the
# right is fully evaluated. This means the swap idiom
#     a, b = b, a
# must snapshot the RHS before reassigning anything on the
# LHS.


# Basic unpacking
t = (1, 2, 3)
a, b, c = t
assert a == 1
assert b == 2
assert c == 3

# Tuple literal on RHS
x, y = (10, 20)
assert x == 10
assert y == 20

# Swap idiom — requires RHS snapshot before LHS updates.
p = 5
q = 10
p, q = q, p
assert p == 10
assert q == 5

# Nested unpacking
n = ((1, 2), (3, 4))
(u, v), (w, z) = n
assert u == 1
assert v == 2
assert w == 3
assert z == 4

# Chained: c relies on a and b being their original values.
a = 1
b = 2
c = 3
a, b, c = b, c, a
assert a == 2
assert b == 3
assert c == 1


# From a tuple-returning function.
def pair() -> tuple:
    return (100, 200)


fp, fq = pair()
assert fp == 100
assert fq == 200
