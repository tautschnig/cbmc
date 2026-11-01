// Copyright 2011 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file of the Go distribution.
// Changes for CBMC's Go front-end: the state is a counter that atomic
// sections update; Wait assumes that it is zero. Misuse that the real
// implementation detects only in some interleavings (Add concurrently with
// Wait, reuse before Wait returned) is not reported.

package sync

import (
	"cprover"
	"cprover/rt"
)

// A WaitGroup waits for a collection of goroutines or tasks to finish.
type WaitGroup struct {
	n int
}

// Add adds delta, which may be negative, to the WaitGroup counter. If the
// counter goes negative, Add panics.
func (wg *WaitGroup) Add(delta int) {
	_ = *wg
	rt.AtomicBegin()
	wg.n += delta
	v := wg.n
	rt.AtomicEnd()
	if v < 0 {
		panic("sync: negative WaitGroup counter")
	}
}

// Done decrements the WaitGroup counter by one.
func (wg *WaitGroup) Done() {
	wg.Add(-1)
}

// Wait blocks until the WaitGroup counter is zero.
func (wg *WaitGroup) Wait() {
	_ = *wg
	rt.AtomicBegin()
	cprover.Assume(wg.n == 0)
	rt.AtomicEnd()
}

// Go calls f in a new goroutine and adds that task to the WaitGroup. When f
// returns, the task is removed from the WaitGroup. If f panics, the program
// crashes (the panic is not recovered).
func (wg *WaitGroup) Go(f func()) {
	wg.Add(1)
	go func() {
		defer func() {
			if x := recover(); x != nil {
				panic(x)
			}

			wg.Done()
		}()
		f()
	}()
}
