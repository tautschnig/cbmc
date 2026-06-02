# PLR: Python ints are unbounded; 10**20 == 100000000000000000000 > 0.
# This is the behaviour we want but cannot get with the default 64-bit
# int model (differential2 §2): 10**20 overflows signedbv[64] so the
# property fails. See intrinsic-int-overflow-unbounded for the variant
# that restores PLR semantics via --python-unbounded-ints.
x: int = 10**20
assert x > 0
