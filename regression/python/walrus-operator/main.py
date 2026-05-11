# PEP 572: walrus operator :=
# Binds as a side-effect of an expression.


# In an if-condition.
n = 10
if (m := n * 2) > 15:
    assert m == 20


# In a while-condition — side effects re-execute every
# iteration.
i = 0
while (x := i + 1) < 5:
    i = x
assert i == 4


# In an expression list.
y = (z := 7) + 3
assert z == 7
assert y == 10
