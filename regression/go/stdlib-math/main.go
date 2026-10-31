// The model of package math uses the IEEE 754 representations (Go1.27
// §Numeric_types: float64 is "the set of all IEEE 754 64-bit floating-point
// numbers"), as the real package does.
package main

import (
	"cprover"
	"math"
)

func main() {
	cprover.Assert(math.Abs(-2.5) == 2.5 && math.Signbit(math.Copysign(0, -1)),
		"abs, signbit")
	cprover.Assert(math.Floor(-1.5) == -2 && math.Ceil(1.2) == 2 &&
		math.Trunc(-1.7) == -1, "floor, ceil, trunc")
	cprover.Assert(math.IsNaN(math.NaN()) && math.IsInf(math.Inf(-1), -1),
		"nan, inf")
	cprover.Assert(math.Max(math.NaN(), math.Inf(1)) == math.Inf(1),
		"Max(NaN, +Inf) = +Inf")
	cprover.Assert(math.Float64bits(1) == 0x3FF0000000000000, "bits")
	cprover.Assert(math.MaxInt8 == 127 && math.Pi > 3.14, "constants")

	// For all float64 values.
	x := cprover.Nondet[float64]()
	if math.Abs(x) < 1<<52 { // then f+1 is exact
		f := math.Floor(x)
		cprover.Assert(f <= x && f+1 > x && math.Trunc(f) == f, "floor")
		i, frac := math.Modf(x)
		cprover.Assert(i+frac == x, "modf")
	}
	cprover.Assert(math.Float64frombits(math.Float64bits(x)) == x ||
		math.IsNaN(x), "bits round trip")
	cprover.Assert(!math.Signbit(math.Abs(x)), "abs is not negative")
}
