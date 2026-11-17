# Plain digits of the base, with a sign or leading zeros, parse exactly.
s = nondet_string(5)
__CPROVER_assume(s == '-001F')
assert int(s, 16) == -31
