// The second send on a channel with a buffer of one, which no goroutine
// receives from, blocks forever; WaitGroup.Wait then waits for it.
package main

import "sync"

func main() {
	var wg sync.WaitGroup
	ch := make(chan int, 1)
	for i := 0; i < 2; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			ch <- i // second send blocks: buffer of 1, never received
		}()
	}
	wg.Wait() // deadlock
}
