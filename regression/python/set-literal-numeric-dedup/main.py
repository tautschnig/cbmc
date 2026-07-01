# PLR §3: set elements dedup by Python numeric equality (1 == 1.0 == True, all
# hash equal), so `{1, 1.0}` has ONE element. The set-literal builder now
# normalises int / bool / integral-float constants to a canonical integer
# (python_numeric_key) before building the bitmap, so they map to the same bit.
# CPython: len == 1, so this `assert len({1, 1.0}) == 2` raises. Expected: FAILED.
assert len({1, 1.0}) == 2
