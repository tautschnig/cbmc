# Nondet string operations
s: str = nondet_str()
__CPROVER_assume(len(s) == 3)
assert len(s) > 0
