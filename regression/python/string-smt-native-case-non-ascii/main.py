# Native SMT-String back-end: the case maps are exact for ASCII only; a
# character beyond ASCII may map to another one ('é'.upper() == 'É') or to
# several ('ß'.upper() == 'SS'). This assertion fails.
s = nondet_str()
if s == "é":
    assert s.swapcase() == "é"
