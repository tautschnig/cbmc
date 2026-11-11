# Native SMT-String back-end: chr() of a code point beyond SMT-LIB's
# character range (0x2FFFF) is still a one-character string, although
# str.from_code yields "" there.
k = nondet_int()
if k == 200000:
    assert len(chr(k)) == 1
if k == 233:
    assert chr(k) == "é"
