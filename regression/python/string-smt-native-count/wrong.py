s = nondet_str()
t = nondet_str()
if s == "aaa" and t == "aa":
    assert s.count(t) == 1
    assert s.count(t) == 2
