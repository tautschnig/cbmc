// Copyright 2009 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file of the Go distribution.
// Changes for CBMC's Go front-end: the state is a writer flag and a reader
// count that atomic sections update; blocking assumes that the lock is
// available. A pending writer does not block new readers (which only
// removes executions in which they wait for it).

package sync

import (
	"cprover/rt"
)

// A RWMutex is a reader/writer mutual exclusion lock. The zero value is an
// unlocked mutex.
type RWMutex struct {
	writer  bool
	readers int
}

// RLock locks rw for reading.
func (rw *RWMutex) RLock() {
	_ = *rw
	rt.AtomicBeginWhen(!rw.writer)
	rw.readers++
	rt.AtomicEnd()
}

// TryRLock tries to lock rw for reading and reports whether it succeeded.
func (rw *RWMutex) TryRLock() bool {
	_ = *rw
	rt.AtomicBegin()
	ok := !rw.writer
	if ok {
		rw.readers++
	}
	rt.AtomicEnd()
	if !ok {
		// A failing TryRLock does not synchronise with what it observes.
		rt.RaceObserve(rw)
	}
	return ok
}

// RUnlock undoes a single RLock call. It is a run-time error if rw is not
// locked for reading on entry to RUnlock.
func (rw *RWMutex) RUnlock() {
	_ = *rw
	rt.AtomicBegin()
	if rw.readers <= 0 {
		rt.Fatal("sync: RUnlock of unlocked RWMutex")
	}
	rw.readers--
	rt.AtomicEnd()
}

// Lock locks rw for writing.
func (rw *RWMutex) Lock() {
	_ = *rw
	rt.AtomicBeginWhen(!rw.writer && rw.readers == 0)
	rw.writer = true
	rt.AtomicEnd()
}

// TryLock tries to lock rw for writing and reports whether it succeeded.
func (rw *RWMutex) TryLock() bool {
	_ = *rw
	rt.AtomicBegin()
	ok := !rw.writer && rw.readers == 0
	if ok {
		rw.writer = true
	}
	rt.AtomicEnd()
	if !ok {
		// A failing TryLock does not synchronise with what it observes.
		rt.RaceObserve(rw)
	}
	return ok
}

// Unlock unlocks rw for writing. It is a run-time error if rw is not locked
// for writing on entry to Unlock.
func (rw *RWMutex) Unlock() {
	_ = *rw
	rt.AtomicBegin()
	if !rw.writer {
		rt.Fatal("sync: Unlock of unlocked RWMutex")
	}
	rw.writer = false
	rt.AtomicEnd()
}

// RLocker returns a Locker interface that implements the Lock and Unlock
// methods by calling rw.RLock and rw.RUnlock.
func (rw *RWMutex) RLocker() Locker {
	return (*rlocker)(rw)
}

type rlocker RWMutex

func (r *rlocker) Lock()   { (*RWMutex)(r).RLock() }
func (r *rlocker) Unlock() { (*RWMutex)(r).RUnlock() }
