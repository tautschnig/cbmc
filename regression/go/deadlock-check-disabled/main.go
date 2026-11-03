// With --go-no-deadlock-check, deadlocks are not reported.
package main

func main() {
	ch := make(chan int)
	ch <- 1 // no receiver: deadlock
}
