# int(s, 16) of a symbolic string must not be parsed in base 10: this
# assertion fails for s == "ff".
s = nondet_str()
if s == "ff":
    assert int(s, 16) != 255
