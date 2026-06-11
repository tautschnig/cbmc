s = nondet_string(5)
assume(s == "  ab ")
# Python whitespace strip on a symbolic string
assert s.strip() == "ab"
assert s.lstrip() == "ab "
assert s.rstrip() == "  ab"

# A control byte (\x01) is NOT Python whitespace, so strip keeps it
# (unlike Java's trim, which removes everything <= 0x20).
t = nondet_string(2)
assume(t == "\x01a")
assert t.strip() == "\x01a"
assert len(t.strip()) == 2
