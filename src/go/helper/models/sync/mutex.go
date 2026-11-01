// Copyright 2009 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file of the Go distribution.
// Changes for CBMC's Go front-end: the state is a flag that atomic
// sections update; blocking assumes that the lock is free (so only the
// executions in which it is acquired continue); fatal errors are those of
// the runtime.

// Package sync models the standard library's package sync (go1.27):
// Mutex, RWMutex, WaitGroup, Once, OnceFunc, OnceValue and OnceValues. The
// program is type-checked against this package, so other declarations
// (Cond, Map, Pool) are not available. Deadlocks are not reported: a
// goroutine that blocks forever does not continue.
package sync

import (
	"cprover"
	"cprover/rt"
)

// A Locker represents an object that can be locked and unlocked.
type Locker interface {
	Lock()
	Unlock()
}

// A Mutex is a mutual exclusion lock. The zero value is an unlocked mutex.
type Mutex struct {
	locked bool
}

// Lock locks m. If the lock is already in use, the calling goroutine blocks
// until the mutex is available.
func (m *Mutex) Lock() {
	_ = *m // a nil m panics outside the atomic section
	rt.AtomicBegin()
	cprover.Assume(!m.locked)
	m.locked = true
	rt.AtomicEnd()
}

// TryLock tries to lock m and reports whether it succeeded.
func (m *Mutex) TryLock() bool {
	_ = *m
	rt.AtomicBegin()
	ok := !m.locked
	m.locked = true
	rt.AtomicEnd()
	return ok
}

// Unlock unlocks m. It is a run-time error if m is not locked on entry to
// Unlock.
func (m *Mutex) Unlock() {
	_ = *m
	rt.AtomicBegin()
	if !m.locked {
		rt.Fatal("sync: unlock of unlocked mutex")
	}
	m.locked = false
	rt.AtomicEnd()
}
