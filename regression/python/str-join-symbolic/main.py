# list() and reversed() of a symbolic string give its characters, and
# str.join of a list of strings concatenates them (it raised a spurious
# TypeError for "".join(reversed(s))).
s = nondet_str()
if s == "abc":
    l = list(s)
    assert len(l) == 3
    assert l[0] == "a" and l[2] == "c"
    assert "".join(reversed(s)) == "cba"
a = nondet_str()
if a == "x":
    l2 = [a, "yz"]
    l2.append("w")
    assert "-".join(l2) == "x-yz-w"
if s == "é1":
    assert len(list(s)) == 2
