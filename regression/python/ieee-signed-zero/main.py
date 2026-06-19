# IEEE-754 signed zero (PLR §4.4 / §6.10.1). copysign and the magnitude
# of a complex number depend on the sign BIT, which is independent of
# ordering (-0.0 is not < 0.0 yet its sign bit is set). Centralised in
# util/ieee_float (ieee_signbit / ieee_fabs / ieee_copysign).
import math

# copysign reads the sign bit (so -0.0 is negative).
assert math.copysign(1.0, -0.0) == -1.0
assert math.copysign(1.0, 0.0) == 1.0
assert math.copysign(1.0, -5.0) == -1.0

# abs(float) is a magnitude: abs(-0.0) is +0.0.
assert math.copysign(1.0, abs(-0.0)) == 1.0

# complex() preserves the sign of a negative-zero imaginary part.
z = complex(-0.0, -0.0)
assert math.copysign(1.0, z.real) == -1.0
assert math.copysign(1.0, z.imag) == -1.0

# conjugate flips the imaginary sign: -(-0.0) == +0.0.
c = z.conjugate()
assert math.copysign(1.0, c.real) == -1.0
assert math.copysign(1.0, c.imag) == 1.0

# abs(complex) is a non-negative magnitude: +0.0, not -0.0.
assert math.copysign(1.0, abs(complex(-0.0, -0.0))) == 1.0

# 0+0j is falsy regardless of the sign of zero (IEEE 0.0 == -0.0).
assert bool(complex(-0.0, 0.0)) == False
assert bool(complex(0.0, -0.0)) == False
assert bool(complex(-0.0, -0.0)) == False
assert bool(complex(1.0, 0.0)) == True
