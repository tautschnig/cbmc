// Go1.27 §Goto_statements: "A "goto" statement transfers control to the
// statement with the corresponding label within the same function."
// §Label_scopes: labels are not block scoped.
package main

import "cprover"

func sum(n int) int {
	s, i := 0, 0
Loop:
	if i < n {
		s += i
		i++
		goto Loop
	}
	return s
}

func main() {
	cprover.Assert(sum(5) == 10, "backward goto as a loop")

	x := cprover.Nondet[int]()
	r := 0
	if x > 0 {
		goto Positive
	}
	r = -1
	goto Done
Positive:
	r = 1
Done:
	cprover.Assert((x > 0) == (r == 1), "forward goto out of a block")
}
