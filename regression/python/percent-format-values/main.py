# printf-style formatting of int and str values, constant and symbolic.
def nondet_int() -> int: ...


def label(k: int, name: str) -> str:
    return "slot %d of %s" % (k, name)


assert "%5d|%-4d|%04x|%#o" % (42, 7, 171, 8) == "   42|7   |00ab|0o10"
assert "%s/%3s/%.2s" % ("ab", "c", "xyz") == "ab/  c/xy"
assert "100%% %+d" % 3 == "100% +3"

k = nondet_int()
__CPROVER_assume(0 <= k < 1000)
s = label(k, "rack")
assert s.startswith("slot ")
assert s.endswith(" of rack")
w = "%03d" % k
assert len(w) == 3
assert int(w) == k
