// cprover.Assert on a property that fails for some nondeterministic input.
package main

import "cprover"

func main() {
	x := cprover.Nondet[int]()
	cprover.Assert(x != 3, "x is not 3")
}
