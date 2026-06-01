# Differential unsoundness witness §6 (cbmc-py-differential UNSOUNDNESS.md):
# list subscript ASSIGNMENT to an out-of-range index must raise
# IndexError, the same as the read path. Previously the write path
# wrote into the size-64 backing array with no bounds check, so a
# real IndexError was silently missed (VERIFICATION SUCCESSFUL on a
# program that raises under CPython 3.13) — a false negative.
lst = [1, 2, 3]
lst[5] = 9  # CPython: IndexError: list assignment index out of range
assert True
