# PLR §6.5: complex ** complex / complex ** float via
#   z**w = exp(w * log(z))
# Constant-folded when all components are compile-time
# constants. Uses host std::log/exp/cos/sin via the
# converter; CBMC's solver doesn't carry these axioms.

# Real^float
z1 = complex(4, 0)
w1 = z1 ** 0.5
assert abs(w1.real - 2.0) < 1e-5
assert abs(w1.imag) < 1e-5

# Negative-exponent reciprocal
z2 = complex(4, 0)
w2 = z2 ** (-0.5)
assert abs(w2.real - 0.5) < 1e-5
assert abs(w2.imag) < 1e-5

# Pure-imag^pure-imag
z3 = complex(1, 0)
w3 = z3 ** complex(0, 1)
assert abs(w3.real - 1.0) < 0.01
assert abs(w3.imag) < 0.01

# Negative real ** even integer (real result)
z4 = complex(-2, 0)
w4 = z4 ** 2
assert abs(w4.real - 4.0) < 1e-5
assert abs(w4.imag) < 1e-5
