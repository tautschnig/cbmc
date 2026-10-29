// Go1.27 §Calls: "Calling a nil function value causes a run-time panic."
// The panic happens after the function value and the arguments have been
// evaluated.
package main

import "cprover"

var evaluated bool

func arg() int {
	evaluated = true
	return 1
}

func main() {
	var f func(int)
	if cprover.Nondet[bool]() {
		f = func(int) {}
	}
	f(arg())
	cprover.Assert(evaluated, "only reached if f was not nil")
}
