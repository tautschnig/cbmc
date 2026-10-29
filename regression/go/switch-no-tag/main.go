// Go1.27 §Expression_switches: "A missing switch expression is equivalent
// to the boolean value true."
package main

import "cprover"

func sign(x int) int {
	switch {
	case x < 0:
		return -1
	case x == 0:
		return 0
	}
	return 1
}

func main() {
	x := cprover.Nondet[int]()
	s := sign(x)
	cprover.Assert((x < 0) == (s == -1), "negative")
	cprover.Assert((x == 0) == (s == 0), "zero")
	cprover.Assert((x > 0) == (s == 1), "positive")

	// With only a simple statement, the expression is still missing.
	switch y := x; {
	case y != x:
		cprover.Assert(false, "unreachable")
	}
}
