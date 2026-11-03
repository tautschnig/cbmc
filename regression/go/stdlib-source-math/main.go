// Package math is its real source (the portable Go of architectures
// without assembly), with bit conversions and rounding modelled.
package main

import (
	"cprover"
	"math"
)

func main() {
	cprover.Assert(math.Sqrt(4) == 2 && math.IsNaN(math.Sqrt(-1)), "Sqrt")
	fr, exp := math.Frexp(8)
	cprover.Assert(fr == 0.5 && exp == 4, "Frexp")
	cprover.Assert(math.Ldexp(0.5, 4) == 8, "Ldexp")
	cprover.Assert(math.Log2(8) == 3, "Log2")
	cprover.Assert(math.Pow(2, 10) == 1024, "Pow")
	cprover.Assert(math.Floor(-1.5) == -2 && math.MaxInt64 > 0, "Floor")
}
