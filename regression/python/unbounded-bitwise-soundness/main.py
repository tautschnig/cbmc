# Soundness: under --python-unbounded-ints (the sound int mode), bitwise &/|/^
# must not truncate the operand to 64-bit. (2**70) & (2**70) is 2**70 (huge),
# not 0. The old code cast to signedbv[64] and wrapped (a leak). These
# genuinely-false assertions must FAIL.
x = 2 ** 70
assert (x & x) == 0
assert (x | 0) == 0
assert (x ^ 0) == 0
assert ((2 ** 70) & (2 ** 70)) == 0
