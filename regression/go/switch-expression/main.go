// Go1.27 §Expression_switches: "the switch expression is evaluated and the
// case expressions, which need not be constants, are evaluated
// left-to-right and top-to-bottom; the first one that equals the switch
// expression triggers execution of the statements of the associated case;
// the other cases are skipped. If no case matches and there is a "default"
// case, its statements are executed." §Switch_statements: "The switch
// expression is evaluated exactly once in a switch statement."
package main

import "cprover"

var calls, trace int

func tag(x int) int {
	calls++
	return x
}

func probe(id, v int) int {
	trace = trace*10 + id
	return v
}

func classify(x int) int {
	switch tag(x) {
	default: // may appear anywhere, but is only taken if nothing matches
		return -1
	case 0, 1, 2:
		return 1
	case probe(1, 3), probe(2, 4):
		return 2
	}
}

func main() {
	x := cprover.Nondet[int]()
	r := classify(x)
	cprover.Assert(calls == 1, "switch expression evaluated exactly once")
	cprover.Assert((x >= 0 && x <= 2) == (r == 1), "first matching case")
	cprover.Assert((x == 3 || x == 4) == (r == 2), "non-constant cases")
	cprover.Assert((x < 0 || x > 4) == (r == -1), "default when none match")

	// Case expressions are evaluated in order, stopping at the first match.
	trace = 0
	classify(3)
	cprover.Assert(trace == 1, "probe(2, 4) not evaluated after a match")
	trace = 0
	classify(4)
	cprover.Assert(trace == 12, "probes evaluated left to right")

	// The optional simple statement runs before the switch expression; no
	// matching case and no default means nothing is executed.
	n := 0
	switch y := x * 0; y {
	case 1:
		n = 1
	}
	cprover.Assert(n == 0, "no case matched and no default")
}
