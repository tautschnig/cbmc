# Floor division and modulo with a negative divisor, on unbounded ints.
def nondet_int() -> int: ...


a = nondet_int()
__CPROVER_assume(-50 < a < 50)
q, r = divmod(a, -3)
assert q == a // -3 and r == a % -3
assert q * -3 + r == a
assert -3 < r < 0
assert divmod(-7, 2) == (-4, 1) and -7 // 2 == -4
