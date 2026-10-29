// Go1.27 §Arithmetic_operators (Integer operators): "If the divisor is
// zero at run time, a run-time panic occurs." Modelled as a property that
// fails if the divisor can be zero; after a panic execution ends.
package main

import "cprover"

func main() {
	d := cprover.Nondet[int]()
	_ = 100 / d // may panic
	e := cprover.Nondet[int]()
	cprover.Assume(e != 0)
	_ = 100 % e // cannot panic
}
