# removeprefix/removesuffix of symbolic strings (refined string solver).
s = nondet_str()
p = nondet_str()
if s == "abcab" and p == "ab":
    assert s.removeprefix(p) == "cab"
    assert s.removesuffix(p) == "abc"
    assert s.removeprefix("b") == "abcab"
    assert s.removesuffix("") == "abcab"
    assert s.removeprefix(s) == ""
    assert s.removesuffix("abcabx") == "abcab"
    assert len(s.removeprefix(p)) == 3

def f(x: str) -> str:
    return x.removeprefix("a")

u = nondet_str()
v = nondet_str()
if u == "ab" and v == "ac":
    assert f(u) == "b"
    assert f(v) == "c"
    n = 0
    for w in [u, v, "xa"]:
        if w.removesuffix("b") == "a":
            n += 1
    assert n == 1
