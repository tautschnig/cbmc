// A call of a standard-library function without a model fails a property
// only where it is reached: here, in a branch that the function does not
// take, so that the rest of the function is verified.
package main

import (
	"cprover"
	"strings"
)

func label(n int) string {
	if n > 1000 {
		return strings.ToUpper("large")
	}
	return "small"
}

func main() {
	n := cprover.Nondet[int]()
	cprover.Assume(0 <= n && n < 100)
	cprover.Assert(label(n) == "small", "small")
}
