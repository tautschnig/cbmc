# More than 8 occurrences: the count is only bounded.
u = nondet_str()
if u == "aaaaaaaaaaaa":
    assert u.count("a") >= 9
    assert u.count("aaaa", 1) == 2
