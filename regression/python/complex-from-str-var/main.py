# PLR 6.10.1: complex(str) parses the string; works for a literal AND a
# variable holding a constant string.
z1 = complex("5+6j")
assert z1.real == 5.0
assert z1.imag == 6.0

s = "5+6j"
z2 = complex(s)
assert z2.real == 5.0
assert z2.imag == 6.0

t = "3-4j"
z3 = complex(t)
assert z3.real == 3.0
assert z3.imag == -4.0
