// Go1.27 §Go_statements: "Calls of built-in functions are restricted as
// for expression statements." The arguments are evaluated in the calling
// goroutine. A panic that the new goroutine does not recover terminates
// the program (§Handling_panics).
package main

import "cprover"

func main() {
	done := make(chan int)
	go close(done)
	_, ok := <-done
	cprover.Assert(!ok, "the goroutine closed the channel")

	m := map[int]int{1: 1, 2: 2}
	finished := make(chan bool)
	go func() {
		defer close(finished)
		delete(m, 1)
	}()
	<-finished
	cprover.Assert(len(m) == 1, "a goroutine deleted an entry")

	go println("hello")
	go recover()
	go panic("boom")
	<-make(chan int)
}
