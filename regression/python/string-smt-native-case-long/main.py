# Native SMT-String back-end: the case maps are built from one mapped
# character per position up to --python-max-string-length. A longer subject
# (here a concatenation of bounded strings) must stay feasible: its result
# has the subject's length, its leading characters are mapped, and the rest
# are unconstrained.
s = nondet_string(2)
t = s + s + s
u = t.upper()
if s == "ab":
    assert u[0:4] == "ABAB"
if s == "aZ":
    assert s.upper() == "AZ"
    assert s.lower() == "az"
    assert s.swapcase() == "Az"
assert len(u) == len(t)
