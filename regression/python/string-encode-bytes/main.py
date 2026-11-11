# str.encode() returns bytes.
s = nondet_str()
x = "a b".encode()
assert x == b"a b"
assert "a b".encode("ascii") == b"a b"
assert "\xe9".encode() == b"\xc3\xa9"
assert "\xe9".encode("latin-1") == b"\xe9"
assert "a b".encode(encoding="ascii") == b"a b"
try:
    "\xe9".encode("ascii")
    assert False
except UnicodeEncodeError:
    pass
y = s.encode()
if s == "ab":
    assert len(y) >= 0
