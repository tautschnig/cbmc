# 'ß'.upper() is 'SS': the length may change, and this assertion fails.
s = nondet_str()
if s == "ß":
    assert len(s.upper()) == 1
