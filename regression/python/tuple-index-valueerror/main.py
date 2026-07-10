# PLR §6.10: tuple.index(v) raises ValueError when v is absent -- for a constant
# tuple with either a constant OR a computed (symbolic) numeric arg. Found by the
# widened random fuzzer. CPython: ValueError; expected: VERIFICATION FAILED.
t = (3, 7)
b = 4 - 4  # computed 0, not in t
i = t.index(b)
