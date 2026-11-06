// A data race is fatal: properties that may be checked after a race are
// UNKNOWN instead of SUCCESS.
package main

var x int
var other int

func main() {
	x = 0 // before any goroutine starts
	done := make(chan bool)
	go func() {
		x = 1
		if other != 0 {
			panic("in the goroutine")
		}
		done <- true
	}()
	x = 2
	<-done
	other = x
	if other == 3 {
		panic("after")
	}
}
