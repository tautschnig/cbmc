# 16 hex digits can exceed the 64-bit int model.
w = nondet_string(16)
for c in w:
    __CPROVER_assume(c in '0123456789abcdef')
x = int(w, 16)
assert w[0] != 'f'
