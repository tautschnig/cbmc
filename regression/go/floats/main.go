// Go1.27 §Numeric_types: "float32 the set of all IEEE 754 32-bit
// floating-point numbers", "float64 the set of all IEEE 754 64-bit
// floating-point numbers". §Floating_point_operators: "The result of a
// floating-point or complex division by zero is not specified beyond the
// IEEE 754 standard; whether a run-time panic occurs is
// implementation-specific." (gc does not panic.) §Comparison_operators:
// "Two floating-point values are compared as defined by the IEEE 754
// standard."
package main

import "cprover"

func main() {
	x, y := 0.1, 0.2
	cprover.Assert(x+y != 0.3 && x+y == 0.30000000000000004, "rounding")
	cprover.Assert(x*3 == 0.30000000000000004 && x/3 == 0.03333333333333333,
		"multiplication and division round to nearest")

	var f32 float32 = 0.1
	cprover.Assert(float64(f32) != 0.1 && f32 == float32(x), "float32")
	var p24 float32 = 1 << 24
	cprover.Assert(p24+1 == p24 && float64(p24)+1 != float64(p24),
		"float32 has a 24-bit significand")

	zero := 0.0
	inf := 1 / zero
	nan := zero / zero
	cprover.Assert(inf > 1e308 && -1/zero < -1e308, "infinities")
	cprover.Assert(nan != nan && !(nan < 0) && !(nan > 0) && !(nan == nan),
		"NaN is unordered")
	cprover.Assert(inf-inf != inf-inf, "Inf - Inf is NaN")

	negZero := -zero
	cprover.Assert(negZero == zero && 1/negZero == -inf, "negative zero")

	n := 1.5
	n++
	n *= 2
	cprover.Assert(n == 5 && -n == -5, "++, op= and negation")

	big := 1e308
	cprover.Assert(big*10 == inf, "overflow to infinity")
	tiny := 5e-324
	cprover.Assert(tiny/2 == 0 && tiny > 0, "underflow, subnormals")
}
