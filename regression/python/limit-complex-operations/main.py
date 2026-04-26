# PLR §3.2: complex numbers
z = complex(1, 2)
assert z.real == 1.0
assert z.imag == 2.0
w = complex(3, 4)
s = z + w
assert s.real == 4.0
