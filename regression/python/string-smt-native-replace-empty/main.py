# Native SMT-String back-end: str.replace with a symbolic `old` that may be
# empty. CPython inserts `new` at every boundary for an empty `old`
# ("ab".replace("", "-") == "-a-b-"), whereas SMT-LIB's str.replace_all
# leaves the string unchanged.
s = nondet_str()
t = nondet_str()
if t == "" and s == "ab":
    r = s.replace(t, "-")
    assert r.startswith("-")
    assert r.endswith("-")
if t == "b" and s == "abab":
    assert s.replace(t, "-") == "a-a-"
