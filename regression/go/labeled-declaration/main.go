// Go1.27 §Labeled_statements: "A labeled statement may be the target of a
// goto, break or continue statement." The label does not open a scope: a
// variable that the labelled statement declares is in scope until the end
// of the enclosing block (§Declarations_and_scope), and a goto back to the
// label declares it anew.
package main

import "cprover"

func retry(x int64) float64 {
	tries := 0
again:
	f := float64(x) / 4
	if f >= 1 {
		x = 1
		tries++
		goto again
	}
	return f + float64(tries)
}

func main() {
	cprover.Assert(retry(2) == 0.5, "f keeps its value after the label")
	cprover.Assert(retry(8) == 1.25, "and is declared anew by the goto")
start:
	n := 3
	n++
	cprover.Assert(n == 4, "a declaration with a label that is not used")
	if n < 4 {
		goto start
	}
}
