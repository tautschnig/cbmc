// Copyright 2009 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file of the Go distribution.
// Changes for CBMC's Go front-end: the done flag is a plain field, read in
// an atomic section.

package sync

import "cprover/rt"

// Once is an object that will perform exactly one action.
type Once struct {
	done bool
	m    Mutex
}

// Do calls the function f if and only if Do is being called for the first
// time for this instance of Once. No call to Do returns until the one call
// to f returns; if f panics, Do considers it to have returned.
func (o *Once) Do(f func()) {
	_ = *o
	rt.AtomicBegin()
	done := o.done
	rt.AtomicEnd()
	if !done {
		o.doSlow(f)
	}
}

func (o *Once) doSlow(f func()) {
	o.m.Lock()
	defer o.m.Unlock()
	if !o.done {
		o.call(f)
	}
}

// call calls f and then sets done, also if f panics (a separate function,
// so that the deferred calls of doSlow are unconditional).
func (o *Once) call(f func()) {
	defer o.setDone()
	f()
}

func (o *Once) setDone() {
	rt.AtomicBegin()
	o.done = true
	rt.AtomicEnd()
}
