// Go1.27 §Min_and_max: min/max compute the smallest/largest of a fixed
// number of arguments of ordered types.
package main

import "cprover"

func main() {
	a, b, c := 3, -1, 7
	cprover.Assert(min(a, b, c) == -1, "min")
	cprover.Assert(max(a, b, c) == 7, "max")
	x := cprover.Nondet[int]()
	cprover.Assert(min(x, 0) <= 0 && max(x, 0) >= 0, "bounds")
}
