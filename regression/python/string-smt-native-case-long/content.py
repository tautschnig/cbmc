# A character beyond the per-position bound is unconstrained in the result,
# so it may differ from the mapped subject character: this assertion fails.
s = nondet_string(2)
t = s + s + s
u = t.upper()
if len(s) == 2:
    assert u[5] != "Q"
