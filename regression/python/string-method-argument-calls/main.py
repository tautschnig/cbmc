# The arguments and the receiver of a string method are evaluated exactly
# once: a folded or nondet result dropped a call in an argument, and a
# string argument used for its length and data called it twice.
n = 0


def f() -> str:
    global n
    n += 1
    return "*"


s = nondet_str()
c = nondet_int()
r1 = "ab".rjust(3, f())
assert n == 1
r2 = s.find(f())
assert n == 2
r3 = s.rjust(3, f())
assert n == 3
r4 = "ab".replace(f(), "x")
assert n == 4
r5 = s.count(f())
assert n == 5
r6 = s.startswith(f())
assert n == 6
r7 = f().find("a")
assert n == 7
r8 = s.find(f() if c > 0 else "x")
assert (n == 8) == (c > 0)
