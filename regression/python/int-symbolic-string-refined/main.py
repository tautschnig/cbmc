# int() of a symbolic string with the refined string solver: exact for a
# sign and digits; other strings that Python accepts give some int.
t = nondet_str()
if t == "42":
    assert int(t) == 42
if t == "-7":
    assert int(t) == -7
