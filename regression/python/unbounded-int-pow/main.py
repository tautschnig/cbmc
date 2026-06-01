# PLR 6.5: ** with --python-unbounded-ints. The exponent/base
# constants are integer_typet (not signedbv), so the exact-fold
# path must accept ID_integer; otherwise ** fell through to nondet.
x: int = 2 ** 10
assert x == 1024
big: int = 10 ** 20
assert big > 0
assert big == 100000000000000000000
