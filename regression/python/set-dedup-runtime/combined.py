# Equal set elements are dropped (PLR 6.2.6), also when their equality is
# only known at run time, and in set comprehensions.
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


def g(i: int):
    t = {i, 3}
    if i != 3:
        assert len(t) == 2
    else:
        assert len(t) == 1


g(int(input()))
c = {y for y in ["a", "a"]}
assert len(c) == 1
d = {y + "1" for y in ["a", "b", "a"]}
assert len(d) == 2
assert "b1" in d
assert d == {"a1", "b1"}
y = x + "b"
if y == "ab":
    assert len(set([y, "ab"])) == 1
e = {f(), "a"}
assert n == 1
assert len(e) == 1
