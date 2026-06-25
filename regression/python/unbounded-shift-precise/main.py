# Precision: under --python-unbounded-ints, shifts stay exact for the common
# cases (constant shift; non-negative operand), now computed as x*2**n and
# floor(x/2**n) in the integer domain.
assert (1 << 3) == 8
assert (5 << 2) == 20
assert (1 << 70) > 0
assert (20 >> 2) == 5
assert ((2 ** 70) >> 5) == (2 ** 65)
