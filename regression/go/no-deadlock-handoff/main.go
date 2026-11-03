// A receive that a goroutine's send satisfies does not deadlock.
package main

import "cprover"

func main() {
	ch := make(chan int)
	go func() { ch <- 1 }()
	cprover.Assert(<-ch == 1, "received")
}
