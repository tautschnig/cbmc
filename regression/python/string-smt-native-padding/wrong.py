# "ab".center(5) is "  ab ": this assertion fails.
s = nondet_str()
k = nondet_int()
if s == "ab" and k == 5:
    assert s.center(k) == " ab  "
