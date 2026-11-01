// Go1.27 §Select_statements: "If one or more of the communications can
// proceed, a single one that can proceed is chosen via a uniform
// pseudo-random selection. Otherwise, if there is a default case, that
// case is chosen."
package main

import "cprover"

func main() {
	var nilch chan int
	select {
	case <-nilch:
		cprover.Assert(false, "nil channels never proceed")
	default:
	}

	full := make(chan int, 1)
	full <- 1
	ready := make(chan int, 1)
	ready <- 2
	got := 0
	select {
	case full <- 3:
		cprover.Assert(false, "the buffer is full")
	case got = <-ready:
	default:
		cprover.Assert(false, "a case can proceed")
	}
	cprover.Assert(got == 2, "received")
}
