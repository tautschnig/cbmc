# The assertion fails in CPython.
# The test of a conditional expression was read again after its branch
# ran: `xs.pop() if xs else 0` emptied xs and gave 0.
xs = [1]
r = xs.pop() if xs else 0
assert r == 0, "if_exp"
