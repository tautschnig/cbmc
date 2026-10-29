// Go1.27 §Break_statements: "If there is a label, it must be that of an
// enclosing "for", "switch", or "select" statement, and that is the one
// whose execution terminates." §Continue_statements: "If there is a label,
// it must be that of an enclosing "for" statement, and that is the one
// whose execution advances." Advancing runs the post statement
// (§For_clause).
package main

import "cprover"

func main() {
	found, inner := -1, 0
Outer:
	for i := 0; i < 5; i++ {
		for j := 0; j < 5; j++ {
			inner++
			switch {
			case i*j == 6:
				found = i*10 + j
				break Outer
			case j > i:
				continue Outer
			}
		}
	}
	// i=0: j=0,1 (1 > 0); i=1: j=0..2 (2 > 1); i=2: j=0..3 (2*3 == 6)
	cprover.Assert(found == 23, "break Outer leaves both loops")
	cprover.Assert(inner == 2+3+4, "continue Outer advances the outer loop")

	// A labelled switch: break with its label leaves the switch only.
	n := 0
	for k := 0; k < 3; k++ {
	Sw:
		switch k {
		case 1:
			for {
				break Sw
			}
		}
		n++
	}
	cprover.Assert(n == 3, "break Sw leaves the switch, not the for loop")
}
