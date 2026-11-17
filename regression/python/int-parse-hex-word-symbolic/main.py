# Parse a symbolic word of 15 hex digits.
w = nondet_string(15)
for c in w:
    __CPROVER_assume(c in '0123456789abcdef')
x = int(w, 16)
assert 0 <= x < (1 << 60)
assert (x >> 56) == int(w[0], 16)
assert x % 16 == int(w[14], 16)
