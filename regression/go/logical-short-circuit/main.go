// Go1.27 §Logical_operators: "The left operand is evaluated, and then the
// right if the condition requires it." Neither the call nor the division (and its run-time
// panic check) may be evaluated when the left operand decides the result.
package main

import "cprover"

func mustNotRun() bool {
	cprover.Assert(false, "right operand must not be evaluated")
	return true
}

func main() {
	if false && mustNotRun() {
	}
	if true || mustNotRun() {
	}
	d := cprover.Nondet[int]()
	if d != 0 && 100/d > 1 {
	}
}
