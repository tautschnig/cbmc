# int(s, 16) of a symbolic str with native SMT strings.
s = nondet_string(2)
__CPROVER_assume(s[0] in "0123456789abcdef" and s[1] in "0123456789abcdef")
a = int(s, 16)
assert 0 <= a and a < 256
