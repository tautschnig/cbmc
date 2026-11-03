// A select with a case that a goroutine makes ready does not deadlock.
package main

import "cprover"

func main() {
	a, b := make(chan int), make(chan int)
	go func() { a <- 1 }()
	got := 0
	select {
	case got = <-a:
	case got = <-b:
	}
	cprover.Assert(got == 1, "from a")
}
