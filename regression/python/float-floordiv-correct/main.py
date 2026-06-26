# PLR §6.7: float floor division `//` rounds toward negative infinity and
# returns a float. Regression for a correctness false proof where float `//`
# was computed as plain true division (`7.0 // 2.0` gave 3.5 instead of 3.0).
assert 7.0 // 2.0 == 3.0
assert 9.0 // 4.0 == 2.0
assert 8.0 // 4.0 == 2.0
assert (-7.0) // 2.0 == -4.0   # floors toward -inf
assert 7 // 2.0 == 3.0          # mixed int // float -> float
assert 2.5 // 1.0 == 2.0
