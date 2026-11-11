# count() of symbolic strings with the refined string solver.
s = nondet_str()
t = nondet_str()
k = nondet_int()
if s == "aaaa" and t == "aa" and k == 1:
    assert s.count(t) == 2
    assert s.count("a") == 4
    assert s.count("") == 5
    assert s.count(t, k) == 1
    assert s.count("", k, 2) == 2
    assert s.count("", 3, k) == 0
    assert s.count("b") == 0
    assert s.count("a", -2) == 2
