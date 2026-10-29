// Go1.27 §Arithmetic_operators: the shift count "must be non-negative. If
// the shift count is negative at run time, a run-time panic occurs."
// Unsigned counts cannot be negative and are not checked.
package main

import "cprover"

func main() {
	var x int64 = 1
	s := cprover.Nondet[int]()
	_ = x << s // may panic
	u := cprover.Nondet[uint]()
	_ = x << u // no check
}
