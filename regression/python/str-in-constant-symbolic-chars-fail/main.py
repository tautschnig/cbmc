# Characters of a symbolic string restricted by membership in a constant.
s = nondet_string(4)
for c in s:
    __CPROVER_assume(c in 'xyz')
assert s[2] != 'z'
assert s[0] == 'x' or s[0] == 'y' or s[0] == 'z'
assert 'yz' in 'xyz' and 'zx' not in 'xyz'
