# The nondet result of removeprefix() is no longer than its subject. Without
# that bound the string solver's model could give it a huge length, and the
# check of a not-contains axiom tried to unroll it (and threw).
k = nondet_int()
s = nondet_str()
if k == -5 and s == "aa":
    r = " ".find(s.removeprefix(s), k)
    assert r != 0
