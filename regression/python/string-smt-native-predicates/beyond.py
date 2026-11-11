# A character beyond Latin-1 is not classified: this assertion fails.
s = nondet_str()
if s == "\u20ac":
    assert not s.isalpha()
