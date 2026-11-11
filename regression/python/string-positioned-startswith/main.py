# startswith/endswith with start and end positions test s[start:end], and
# are False when the adjusted start is after the adjusted end.
s = nondet_str()
k = nondet_int()
j = nondet_int()
if s == "hello" and k == 1 and j == 4:
    assert s.startswith("ell", k)
    assert not s.startswith("hel", k)
    assert s.startswith("el", k, j)
    assert not s.startswith("ello", k, j)
    assert s.endswith("ll", 0, j)
    assert not s.endswith("lo", 0, j)
    assert s.endswith("", k, j)
    assert not s.startswith("", 6)
    assert s.startswith("", 5)
    assert not s.endswith("", j, k)
    assert s.startswith("lo", -2)
    assert s.endswith("he", None, -3)
