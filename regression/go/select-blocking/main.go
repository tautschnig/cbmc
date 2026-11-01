// Go1.27 §Select_statements: "If there is no default case, the "select"
// statement blocks until at least one of the communications can proceed."
// Two goroutines that both select communicate with each other.
package main

import "cprover"

func main() {
	a := make(chan int)
	quit := make(chan bool)
	go func() {
		select {
		case a <- 7:
		case <-quit:
		}
	}()
	select {
	case v := <-a:
		cprover.Assert(v == 7, "value")
	case quit <- true:
	}
}
