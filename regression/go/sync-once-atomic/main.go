// The models of sync.Once, sync.OnceValue and sync/atomic.
package main

import (
	"cprover"
	"sync"
	"sync/atomic"
)

func main() {
	var once sync.Once
	calls := 0
	for i := 0; i < 2; i++ {
		once.Do(func() { calls++ })
	}
	cprover.Assert(calls == 1, "once")
	f := sync.OnceValue(func() int { calls++; return 42 })
	cprover.Assert(f() == 42 && f() == 42 && calls == 2, "once value")

	var hits atomic.Int32
	var wg sync.WaitGroup
	wg.Go(func() { hits.Add(1) })
	wg.Go(func() { hits.Add(2) })
	wg.Wait()
	cprover.Assert(hits.Load() == 3, "atomic adds")

	var u uint32
	cprover.Assert(atomic.CompareAndSwapUint32(&u, 0, 5), "swapped")
	cprover.Assert(!atomic.CompareAndSwapUint32(&u, 0, 6) && u == 5, "not swapped")
	var p atomic.Pointer[int]
	x := 7
	cprover.Assert(p.Load() == nil, "nil")
	p.Store(&x)
	cprover.Assert(*p.Load() == 7, "pointer")
}
