# str() of an int and a hex format of an int in one program.
def nondet_int() -> int: ...


n = nondet_int()
__CPROVER_assume(0 <= n < 4096)
dec = str(n)
hx = format(n, "x")
assert int(dec) == n
assert int(hx, 16) == n
