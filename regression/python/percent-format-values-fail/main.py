# The padding is a minimum width: a wider value is not truncated.
def nondet_int() -> int: ...


k = nondet_int()
__CPROVER_assume(0 <= k < 100000)
w = "%03d" % k
assert len(w) == 3
