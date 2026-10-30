// Calling a method of a nil interface value causes a run-time panic (Go1.27
// §Selectors: "If x is of interface type and has the value nil, calling or
// evaluating the method x.f causes a run-time panic.").
package main

import "cprover"

type I interface{ M() int }
type A struct{}

func (A) M() int { return 1 }

func main() {
	var i I
	if cprover.Nondet[bool]() {
		i = A{}
	}
	cprover.Assert(i.M() == 1, "only reached if i is not nil")
}
