# A list comprehension of strings under the native string backend stores its
# elements as string handles (this used to abort in simplify_expr).
n = 0


def f() -> str:
    global n
    n += 1
    return "*"


r = [f() + "a" for i in range(2)]
assert n == 2
assert len(r) == 2
assert r[0] == "*a"
assert r[1] == "*a"
s = [str(i) for i in range(3)]
assert s[2] == "2"
w = [x + "!" for x in ["p", "q"]]
assert w[1] == "q!"
pr = [a + b for a in ["x", "y"] for b in ["1", "2"]]
assert pr[3] == "y2"
