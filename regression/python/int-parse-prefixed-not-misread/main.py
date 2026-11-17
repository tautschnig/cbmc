# int(s, 16) accepts a 0x prefix; the value is 31, not something else.
s = nondet_string(4)
__CPROVER_assume(s == '0x1f')
assert int(s, 16) != 31
