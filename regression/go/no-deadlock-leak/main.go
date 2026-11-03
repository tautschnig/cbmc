// A goroutine that blocks forever is not a deadlock once main returns:
// the program ends.
package main

import "cprover"

func main() {
	ch := make(chan int)
	n := cprover.Nondet[int]()
	go func() { <-ch }()
	cprover.Assert(n == n, "main returns")
}
