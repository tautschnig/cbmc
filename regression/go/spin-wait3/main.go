// A spin-wait loop that never ends stops only its own goroutine: the panic
// of the other goroutine is found.
package main

import (
	"sync/atomic"
	"time"
)

func main() {
	var s int32
	go func() {
		panic("other goroutine")
	}()
	for atomic.LoadInt32(&s) != 1 {
		time.Sleep(time.Millisecond)
	}
}
