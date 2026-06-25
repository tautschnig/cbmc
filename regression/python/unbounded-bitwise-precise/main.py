# Precision: in-range bitwise stays exact under --python-unbounded-ints
# (fit-select to the 64-bit op; constants folded exactly).
assert (5 & 3) == 1
assert (5 | 2) == 7
assert (6 ^ 3) == 5
assert ((2 ** 70) & (2 ** 70)) == (2 ** 70)
