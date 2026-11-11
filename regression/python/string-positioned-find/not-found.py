# A needle longer than the string is not found: this assertion fails.
s = nondet_str()
k = nondet_int()
if s == "ab" and k == 3:
    assert s.rindex("bb", k) == 0
