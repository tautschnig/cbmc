// Go1.27 §Handling_panics: panic stops the ordinary execution; without
// recover the program terminates. A reachable panic is a property
// violation; the code after it is not executed on that path.
package main

import "cprover"

func main() {
	x := cprover.Nondet[int]()
	if x < 0 {
		panic("negative input")
	}
	cprover.Assert(x >= 0, "only reached without a panic")
}
