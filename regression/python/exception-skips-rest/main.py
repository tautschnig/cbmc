# An exception skips the rest of the enclosing blocks up to its handler, also
# inside functions, nested blocks and loops (PLR 8.4); a finally block runs
# whole, and a new exception in it replaces the propagating one.
g = 0


def f(k: int):
    global g
    x = 10 // k
    g = 1


def h(k: int) -> int:
    x = 10 // k
    assert x != 7
    return x


try:
    f(0)
except ZeroDivisionError:
    assert g == 0
try:
    h(0)
except ZeroDivisionError:
    pass
k = nondet_int()
try:
    if k == 0:
        raise ValueError()
        g = 2
except ValueError:
    assert g == 0
n = 0
try:
    for i in range(3):
        if i == 1:
            raise ValueError()
        n += 1
except ValueError:
    assert n == 1


def fin(k: int):
    global g
    try:
        x = 10 // k
    finally:
        h(1)
        g = 5


try:
    fin(0)
except ZeroDivisionError:
    assert g == 5
r = 0
try:
    try:
        x = 10 // 0
    finally:
        y = [1][3]
except ZeroDivisionError:
    assert False
except IndexError:
    r = 1
assert r == 1
