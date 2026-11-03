// The model of the functions of sync/atomic on unsafe.Pointer.
package main

import (
	"cprover"
	"sync"
	"sync/atomic"
	"unsafe"
)

type config struct{ n int }

func main() {
	var p unsafe.Pointer
	a, b := &config{1}, &config{2}
	cprover.Assert(atomic.LoadPointer(&p) == nil, "nil")
	atomic.StorePointer(&p, unsafe.Pointer(a))
	cprover.Assert((*config)(atomic.LoadPointer(&p)).n == 1, "stored")
	cprover.Assert(!atomic.CompareAndSwapPointer(&p, unsafe.Pointer(b), nil),
		"not swapped")
	old := atomic.SwapPointer(&p, unsafe.Pointer(b))
	cprover.Assert(old == unsafe.Pointer(a), "swapped")

	var wg sync.WaitGroup
	wg.Add(1)
	go func() {
		defer wg.Done()
		atomic.CompareAndSwapPointer(&p, unsafe.Pointer(b), unsafe.Pointer(a))
	}()
	wg.Wait()
	cprover.Assert((*config)(atomic.LoadPointer(&p)).n == 1, "swapped back")
}
