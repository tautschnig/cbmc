# Integer powers of complex numbers must be exact (computed by repeated
# multiplication, not the lossy exp(w*log z) form), and must work for a
# symbolic base. Exact equality below would fail under the float-error
# of an exp/log model.
z = complex(0.0, 1.0)
assert (z ** 2) == complex(-1.0, 0.0)   # i^2 = -1, exactly
assert (z ** 0) == complex(1.0, 0.0)    # anything ** 0 = 1
assert (z ** 1) == z                    # identity
assert (complex(1.0, 1.0) ** 2) == complex(0.0, 2.0)
assert (complex(1.0, 1.0) ** 3) == complex(-2.0, 2.0)
# Negative exponent: 1/(1+1j) = 0.5 - 0.5j (exact in binary).
assert (complex(1.0, 1.0) ** -1) == complex(0.5, -0.5)
# Bool exponents behave as 1 / 0.
assert (z ** True) == z
assert (z ** False) == complex(1.0, 0.0)
