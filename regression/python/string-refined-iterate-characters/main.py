# Iterating over a refined string visits its characters, also beyond ASCII,
# where a character has several bytes.
s = nondet_string(4)
n = 0
for c in s:
    assert len(c) == 1
    n += 1
assert n == 4
x = nondet_string(1)
__CPROVER_assume(x.isascii())
t = "é" + x
m = 0
for c in t:
    m += 1
    if m == 1:
        assert c == "é"
assert m == 2
