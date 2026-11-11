# join() of a list literal of symbolic strings, and of an empty list.
s = nondet_str()
t = nondet_str()
u = nondet_str()
if s == "-" and t == "ab" and u == "c":
    assert s.join([t, u]) == "ab-c"
    assert s.join([t]) == "ab"
    assert s.join([]) == ""
    assert "".join([t, u, t]) == "abcab"

