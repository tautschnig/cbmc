# Native SMT-String back-end: partition() returns a 3-tuple, not a string.
# Its elements are nondet, so this assertion fails.
s = nondet_str()
t = nondet_str()
if s == "Ba" and t == " aab":
    r = s.partition(t)[0]
    assert r != "Ba"
