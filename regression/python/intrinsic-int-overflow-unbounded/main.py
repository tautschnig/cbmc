# With --python-unbounded-ints, CBMC models int as the mathematical
# integer type, so 10**20 is exact and the PLR-true property holds.
# This is the flag-restored variant of intrinsic-int-overflow-knownbug
# (differential2 §2): the unsoundness of the default 64-bit model is a
# policy choice with a sound escape hatch.
x: int = 10**20
assert x > 0
