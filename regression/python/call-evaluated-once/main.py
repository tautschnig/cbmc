# A call within an expression is evaluated exactly once: conversions that
# read a string's length and data called it twice.
n = 0


def f() -> str:
    global n
    n += 1
    return "*"


r1 = len(f())
assert n == 1
r2 = f() + "a"
assert n == 2
r3 = f()[0]
assert n == 3
r4 = [f()][0]
assert n == 4
r5 = (lambda: f() + "a")()
assert n == 5
c = nondet_int()
r6 = c > 0 and f() + "a" == "*a"
assert (n == 6) == (c > 0)
m = n
if f():
    pass
assert n == m + 1
assert f()
assert n == m + 2
for ch in f():
    pass
assert n == m + 3
k = 0
while f() and k < 2:
    k += 1
assert n == m + 6
