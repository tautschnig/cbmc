# For an empty `old` the result is not the subject: this assertion fails.
s = nondet_str()
t = nondet_str()
if t == "":
    assert s.replace(t, "x") == s
