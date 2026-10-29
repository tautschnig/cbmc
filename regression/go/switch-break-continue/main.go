// Go1.27 §Break_statements: "A "break" statement terminates execution of
// the innermost "for", "switch", or "select" statement within the same
// function." §Continue_statements: "A "continue" statement begins the next
// iteration of the innermost enclosing "for" loop". So within a switch in
// a loop, break leaves the switch and continue continues the loop.
package main

import "cprover"

func main() {
	iterations, after := 0, 0
	for i := 0; i < 4; i++ {
		iterations++
		switch i {
		case 1:
			break
		case 2:
			continue
		}
		after++
	}
	cprover.Assert(iterations == 4, "break in a switch does not end the loop")
	cprover.Assert(after == 3, "continue in a switch skips the rest")
}
