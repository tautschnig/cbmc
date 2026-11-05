// Copyright 2011 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file of the Go distribution.
// Changes for CBMC's Go front-end: the state is a counter that atomic
// sections update; Wait blocks until it is zero. For the data-race check,
// the first increment from zero reads sema and the first waiter writes it,
// as the real implementation's race annotations do, so that an Add that is
// not synchronised with Wait is a race. Other misuse that the real
// implementation detects only in some interleavings (reuse before Wait
// returned) is not reported.

package sync

import (
	"cprover/rt"
)

// A WaitGroup waits for a collection of goroutines or tasks to finish.
type WaitGroup struct {
	n       int
	waiters int
	sema    uint32 // only for the race annotations
}

// Add adds delta, which may be negative, to the WaitGroup counter. If the
// counter goes negative, Add panics.
func (wg *WaitGroup) Add(delta int) {
	_ = *wg
	rt.AtomicBegin()
	wg.n += delta
	v := wg.n
	if v == 0 {
		wg.waiters = 0
	}
	rt.AtomicEnd()
	if delta > 0 && v == delta {
		// The first increment must be synchronized with Wait.
		rt.RaceRead(&wg.sema)
	}
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
	first := wg.n > 0 && wg.waiters == 0
	if wg.n > 0 {
		wg.waiters++
	}
	rt.AtomicEnd()
	if first {
		// Wait must be synchronized with the first Add.
		rt.RaceWrite(&wg.sema)
	}
	rt.AtomicBeginWhen(wg.n == 0)
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
