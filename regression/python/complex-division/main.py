z1 = 1 + 2j
z2 = 3 + 4j
z3 = z1 / z2
assert abs(z3.real - 0.44) < 0.01
assert abs(z3.imag - 0.08) < 0.01
