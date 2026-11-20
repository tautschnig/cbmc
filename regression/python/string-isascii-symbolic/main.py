# isascii() of a symbolic refined string is whether all its bytes are ASCII.
s = nondet_string(2)
if s.isascii():
    assert ord(s[0]) < 128 and ord(s[1]) < 128
t = "é" + s
assert not t.isascii()
u = "ab" + s
if u.isascii():
    assert s.isascii()
