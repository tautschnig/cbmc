# "aa".replace("a", "b", 1) is "ba". A count that is not a constant was
# taken as "replace all" for native strings, which verified this assertion.
s = nondet_str()
k = nondet_int()
if s == "aa" and k == 1:
    assert s.replace("a", "b", k) == "bb"
