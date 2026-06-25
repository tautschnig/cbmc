# Soundness: under --python-unbounded-ints, shifts must use the mathematical-
# integer domain, not truncate to 64-bit. `1 << 70` is 2**70 (huge), not 0;
# `(2**70) >> 5` is 2**65, not 0. These genuinely-false assertions must FAIL
# (the old code truncated to signedbv64 and false-proved them == 0).
assert (1 << 70) == 0
assert (1 << 70) < 0
assert ((2 ** 70) >> 5) == 0
