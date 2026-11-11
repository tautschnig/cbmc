# replace() of symbolic strings with the refined string solver.
s = nondet_str()
n = nondet_str()
k = nondet_int()
if s == "abab" and n == "x" and k == 1:
    assert s.replace("ab", n, k) == "xab"
    assert s.replace("ab", n, 0) == "abab"
    assert s.replace("ab", n) == "xx"
    assert s.replace("ab", n, k - 3) == "xx"
    assert s.replace("", n, k) == "xabab"
    assert s.replace("", n, 3) == "xaxbxab"
    assert s.replace("b", "", k + 1) == "aa"
    assert s.replace("c", n) == "abab"

def f(x: str) -> str:
    return x.replace("a", "-", 1)

u = nondet_str()
v = nondet_str()
if u == "aba" and v == "ca":
    assert f(u) == "-ba"
    assert f(v) == "c-"
    n = 0
    for w in [u, v, "xa"]:
        if w.replace("a", "") == "c":
            n += 1
    assert n == 1
