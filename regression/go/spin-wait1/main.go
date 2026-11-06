// Spin-wait loops on atomics are awaits: no loop is left to unwind, and the
// accesses that they order do not race.
package main

import (
	"runtime"
	"sync/atomic"
	"time"
)

func main() {
	var x int
	var s int32
	var t atomic.Int32
	go func() {
		x = 1
		atomic.StoreInt32(&s, 1)
	}()
	for atomic.LoadInt32(&s) != 1 {
		runtime.Gosched()
	}
	x = 2
	go func() {
		x = 3
		t.Store(1)
	}()
	for !t.CompareAndSwap(1, 2) {
		time.Sleep(time.Millisecond)
	}
	x = 4
	if t.Load() != 2 || x != 4 {
		panic("unreachable")
	}
}
