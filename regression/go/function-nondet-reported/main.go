// Go1.27 §Function_types: a function value is nil or refers to a function.
// An unconstrained pointer would be neither, so cprover.Nondet of a
// function type is reported rather than modelled unsoundly.
package main

import "cprover"

func main() {
	f := cprover.Nondet[func() int]()
	cprover.Assert(f == nil || f() == 1, "f")
}
