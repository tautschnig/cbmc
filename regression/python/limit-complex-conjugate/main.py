# PLR §3.2: complex.conjugate() returns complex conjugate
z = complex(3, 4)
w = z.conjugate()
assert w.real == 3.0
assert w.imag == -4.0
