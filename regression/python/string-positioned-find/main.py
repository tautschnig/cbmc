# find/rfind/index/rindex with start and end positions, and rfind of native
# strings, which have no last_index_of.
s = nondet_str()
k = nondet_int()
j = nondet_int()
if s == "hello" and k == 1 and j == 4:
    assert s.find("l", k, j) == 2
    assert s.find("o", k, j) == -1
    assert s.find("", k, j) == 1
    assert s.find("", j, k) == -1
    assert s.rfind("l") == 3
    assert s.rfind("l", 0, 3) == 2
    assert s.rfind("") == 5
    assert s.rfind("", k, j) == 4
    assert s.rfind("x") == -1
    assert s.rfind("l", -2) == 3
    assert s.find("lo", -3, None) == 3
    assert s.index("l", k, j) == 2
    try:
        n = s.rindex("h", k)
        assert False
    except ValueError:
        pass
