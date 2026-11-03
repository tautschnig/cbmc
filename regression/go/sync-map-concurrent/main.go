// The model of sync.Map with goroutines: one of two concurrent
// LoadOrStore calls stores. The map is created by the goroutine that runs
// first, so symbolic execution is repeated with the values goroutines
// write, in which allocations of other sizes keep their names.
package main

import (
	"cprover"
	"sync"
)

func main() {
	var m sync.Map
	done := make(chan bool)
	go func() {
		m.LoadOrStore(0, 1)
		done <- true
	}()
	m.LoadOrStore(0, 2)
	<-done
	v, _ := m.Load(0)
	cprover.Assert(v == 1 || v == 2, "one of the stores")
}
