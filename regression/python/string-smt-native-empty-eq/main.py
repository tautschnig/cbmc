# Native SMT-String back-end regression: comparing a string to the empty
# literal "" must verify soundly. Previously `nondet_string(0) == ""` reported
# a spurious VERIFICATION ERROR: the string length is read back as
# `((_ int2bv 64) (str.len s))`, and because `int2bv` reduces modulo 2^64 the
# solver could satisfy `int2bv(str.len s) == 0` with `str.len s == 2^64`,
# producing a model with an astronomically long string that exceeded the
# solver's string-model length cap and broke value parsing. The fix bounds the
# actual `str.len` below 2^63 for every native string symbol, keeping the
# int2bv conversion faithful.

# A length-0 nondet string IS the empty string.
s = nondet_string(0)
assert s == ""
assert len(s) == 0

# A length-2 nondet string is never empty.
t = nondet_string(2)
assert t != ""
assert len(t) == 2

# Empty literal compares equal to itself and unequal to a non-empty literal.
assert "" == ""
assert "x" != ""
