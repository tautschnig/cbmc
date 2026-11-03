// The model of sync.Cond: a goroutine waiting for a condition is woken by
// Signal or Broadcast, and sees the condition when Wait returns.
package main

import (
	"cprover"
	"sync"
)

func main() {
	var mu sync.Mutex
	c := sync.NewCond(&mu)
	ready := 0
	var wg sync.WaitGroup
	for i := 0; i < 2; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			mu.Lock()
			for ready == 0 {
				c.Wait()
			}
			cprover.Assert(ready == 1, "woken after the change")
			mu.Unlock()
		}()
	}
	mu.Lock()
	ready = 1
	mu.Unlock()
	c.Broadcast()
	wg.Wait()

	// Signal without waiters does nothing.
	c.Signal()
}
