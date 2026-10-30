// Go1.27 §Type_assertions: "If the type assertion is false, a run-time
// panic occurs."
package main

import "cprover"

func main() {
	var x any = 1
	if cprover.Nondet[bool]() {
		x = "one"
	}
	n := x.(int)
	cprover.Assert(n == 1, "only reached if x holds an int")
}
