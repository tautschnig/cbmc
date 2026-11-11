# "hello".rfind("l", 0, 3) is 2: this assertion fails.
s = nondet_str()
k = nondet_int()
if s == "hello" and k == 3:
    assert s.rfind("l", 0, k) != 2
