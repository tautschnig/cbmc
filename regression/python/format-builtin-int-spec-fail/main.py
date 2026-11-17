# The digits of a negative value follow its sign; "x" is lower case.
def nondet_int() -> int: ...


v = nondet_int()
__CPROVER_assume(-300 < v < 300)
h = format(v, "x")
assert h != "-ff"
