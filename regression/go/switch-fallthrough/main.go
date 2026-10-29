// Go1.27 §Fallthrough_statements: "A "fallthrough" statement transfers
// control to the first statement of the next case clause in an expression
// "switch" statement." The next clause's case expressions are not tested;
// §Expression_switches: otherwise "control flows to the end of the
// "switch" statement".
package main

import "cprover"

func f(x int) int {
	r := 0
	switch x {
	case 0:
		r += 1
		fallthrough
	case 1:
		r += 10
	case 2:
		r += 100
		fallthrough
	default:
		r += 1000
	case 3:
		r += 10000
	}
	return r
}

func main() {
	cprover.Assert(f(0) == 11, "0 falls through into case 1, stops there")
	cprover.Assert(f(1) == 10, "1 does not fall through")
	cprover.Assert(f(2) == 1100, "2 falls through into the default clause")
	cprover.Assert(f(3) == 10000, "3 is not reached by falling through")
	cprover.Assert(f(7) == 1000, "default does not fall through")
}
