// Go1.27 §If_statements: "The expression may be preceded by a simple
// statement, which executes before the expression is evaluated"; its
// variables are in scope in all branches.
package main

import "cprover"

func sign(x int) int {
	if x < 0 {
		return -1
	} else if x == 0 {
		return 0
	}
	return 1
}

func main() {
	v := cprover.Nondet[int]()
	if s := sign(v); s < 0 {
		cprover.Assert(v < 0, "negative")
	} else if s == 0 {
		cprover.Assert(v == 0, "zero")
	} else {
		cprover.Assert(v > 0 && s == 1, "positive")
	}
}
