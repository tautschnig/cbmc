// Go1.27 §Min_and_max: "For floating-point arguments negative zero, NaN, and
// infinity the following rules apply":
//
//	 x        y    min(x, y)    max(x, y)
//	-0.0    0.0         -0.0          0.0    // negative zero is smaller than (non-negative) zero
//	-Inf      y         -Inf            y    // negative infinity is smaller than any other number
//	+Inf      y            y         +Inf    // positive infinity is larger than any other number
//	 NaN      y          NaN          NaN    // if any argument is a NaN, the result is a NaN
package main

import "cprover"

func main() {
	zero := 0.0
	negZero := -zero
	inf := 1 / zero
	nan := zero / zero
	cprover.Assert(1/min(negZero, zero) == -inf && 1/min(zero, negZero) == -inf,
		"min(-0, 0) is -0")
	cprover.Assert(1/max(negZero, zero) == inf && 1/max(zero, negZero) == inf,
		"max(-0, 0) is 0")
	y := cprover.Nondet[float64]()
	cprover.Assume(y == y)
	cprover.Assert(min(-inf, y) == -inf && max(-inf, y) == y, "-Inf")
	cprover.Assert(min(inf, y) == y && max(inf, y) == inf, "+Inf")
	cprover.Assert(min(nan, y) != min(nan, y) && max(y, nan) != max(y, nan), "NaN")
	cprover.Assert(min(1.5, 2, y) <= 1.5 && max(1.5, y, 3) >= 3, "several arguments")
}
