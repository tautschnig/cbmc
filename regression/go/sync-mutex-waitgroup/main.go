// The models of sync.Mutex and sync.WaitGroup: the increments are mutually
// exclusive, and Wait returns once all goroutines are done.
package main

import (
	"cprover"
	"sync"
)

type counter struct {
	mu sync.Mutex
	n  int
}

func (c *counter) inc() {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.n++
}

func main() {
	var c counter
	var wg sync.WaitGroup
	for i := 0; i < 2; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			c.inc()
		}()
	}
	wg.Go(c.inc)
	wg.Wait()
	cprover.Assert(c.n == 3, "no lost updates")

	var rw sync.RWMutex
	shared := 0
	var wg2 sync.WaitGroup
	wg2.Go(func() { rw.Lock(); shared = 1; rw.Unlock() })
	rw.RLock()
	v := shared
	rw.RUnlock()
	wg2.Wait()
	cprover.Assert(v == 0 || v == 1, "either order")
	cprover.Assert(shared == 1, "written")
}
