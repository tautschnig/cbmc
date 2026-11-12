# partition and rpartition of symbolic strings split at the first (last)
# occurrence of the separator.
s = nondet_str()
t = nondet_str()
if s == "abab" and t == "b":
    r = s.partition(t)
    assert r[0] == "a" and r[1] == "b" and r[2] == "ab"
    q = s.rpartition(t)
    assert q[0] == "aba" and q[1] == "b" and q[2] == ""
if s == "aab" and t == "x":
    r = s.partition(t)
    assert r[0] == "aab" and r[1] == "" and r[2] == ""
    q = s.rpartition(t)
    assert q[0] == "" and q[1] == "" and q[2] == "aab"
if t == ",":
    h, m, rest = s.partition(t)
    assert h + m + rest == s
    assert "," not in h
