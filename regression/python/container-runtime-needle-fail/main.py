# Vacuity twin: the same runtime-built needle with a WRONG expectation
# must still be refuted (CPython: AssertionError).
k = ""
for c in ["a", "b"]:
    k = k + c

d = {"ab": 1, "cd": 2}
assert d.pop(k) == 9
