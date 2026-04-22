def check_positive(x: int) -> bool:
    return x > 0

x: int = nondet_int()
__CPROVER_assume(x > 0 and x < 100)
assert check_positive(x)
