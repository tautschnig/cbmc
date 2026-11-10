# Native SMT-String back-end: int(s) of a symbolic string. SMT-LIB's
# str.to_int only reads unsigned digit strings; the sign is handled by the
# front end.
s = nondet_str()
if s == "-5":
    assert int(s) == -5
if s == "+42":
    assert int(s) == 42
if s == "007":
    assert int(s) == 7
