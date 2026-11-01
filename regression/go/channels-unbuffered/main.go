// Go1.27 §Channel_types: "If the capacity is zero or absent, the channel
// is unbuffered and communication succeeds only when both a sender and
// receiver are ready." §For_range: "For channels, the iteration values
// produced are the successive values sent on the channel until the channel
// is closed."
package main

import "cprover"

func producer(ch chan<- int, n int) {
	for i := 1; i <= n; i++ {
		ch <- i
	}
	close(ch)
}

func main() {
	ch := make(chan int)
	go producer(ch, 2)
	last := 0
	for v := range ch {
		cprover.Assert(v == last+1, "in order")
		last = v
	}
	cprover.Assert(last == 2, "all values")
	_, ok := <-ch
	cprover.Assert(!ok, "closed and empty")

	// The send completes only after the receive started: the write of x
	// before the send is visible after the receive.
	done := make(chan bool)
	x := 0
	go func() {
		x = 42
		done <- true
	}()
	<-done
	cprover.Assert(x == 42, "synchronised")
}
