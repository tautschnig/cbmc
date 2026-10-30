// Floating-point values of arbitrary value (cprover.Nondet), checked for all
// IEEE 754 values, including the properties that rounding breaks.
package main

import "cprover"

func main() {
	x := cprover.Nondet[float64]()
	cprover.Assume(x >= 1 && x <= 1000)
	cprover.Assert(x+1 > x, "no absorption in this range")
	cprover.Assert(x*2/2 == x, "exact: multiplying by two")
	cprover.Assert(x*0.1*10 == x, "not exact: rounding")
	y := cprover.Nondet[float32]()
	cprover.Assert(y == y || y != y, "NaN or not")
}
