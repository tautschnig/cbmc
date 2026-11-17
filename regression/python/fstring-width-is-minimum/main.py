# A format-spec width is a minimum; it never truncates.
def nondet_int() -> int: ...


n = nondet_int()
__CPROVER_assume(0 <= n < 100000)
s = f"{n:03}"
assert len(s) == 3
