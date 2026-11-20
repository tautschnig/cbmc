# The assertion fails in CPython.
s = nondet_string(2)
if not s.isascii():
    assert s.isascii()
