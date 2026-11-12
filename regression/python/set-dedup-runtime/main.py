# Equal set elements are dropped (PLR 6.2.6), also when their equality is
# only known at run time.
n = 0


def f() -> str:
    global n
    n += 1
    return "a"


x = input()
if x == "a":
    s = {x, "a"}
    assert len(s) == 1
else:
    s = {x, "a"}
    assert len(s) == 2
    assert x in s
e = {f(), "a"}
assert n == 1
assert len(e) == 1
