// Go's runtime: `fatal error: all goroutines are asleep - deadlock!` A
// send on an unbuffered channel without a receiver blocks forever.
package main

func main() {
	ch := make(chan int)
	ch <- 1 // no receiver: deadlock
}
