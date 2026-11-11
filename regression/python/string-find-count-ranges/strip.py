# "Bb".strip(s) with a symbolic s is not "Bb": this assertion fails.
s = nondet_str()
if s == "ba":
    assert "Bb".strip(s) == "Bb"
