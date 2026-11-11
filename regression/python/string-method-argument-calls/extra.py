# Methods that native strings do not model still evaluate their arguments.
n = 0


def f() -> str:
    global n
    n += 1
    return "*"


s = nondet_str()
r1 = s.join([f(), f()])
assert n == 2
r2 = s.translate(f())
assert n == 3
r3 = s.split(f())
assert n == 4
