# The positioned searches and rfind on the native back end.
s = nondet_str()
k = nondet_int()
j = nondet_int()
if s == "hello" and k == 1 and j == 4:
    assert s.find("l", k, j) == 2
    assert s.rfind("l") == 3
    assert s.rfind("l", 0, 3) == 2
    assert s.rfind("") == 5
    assert s.rfind("x") == -1
