// Copyright 2011 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file of the Go distribution.
// Changes for CBMC's Go front-end: the notify list is two ticket
// counters, as in the runtime (runtime_notifyListAdd and friends):
// waiters take the next ticket, and notifications advance the ticket
// up to which waiters are woken. Copying a Cond is not detected.

package sync

import "cprover/rt"

// Cond implements a condition variable, a rendezvous point
// for goroutines waiting for or announcing the occurrence
// of an event.
//
// Each Cond has an associated Locker L (often a [*Mutex] or [*RWMutex]),
// which must be held when changing the condition and
// when calling the [Cond.Wait] method.
type Cond struct {
	// L is held while observing or changing the condition
	L Locker

	wait   uint // the ticket of the next waiter
	notify uint // waiters with smaller tickets are woken
}

// NewCond returns a new Cond with Locker l.
func NewCond(l Locker) *Cond {
	return &Cond{L: l}
}

// Wait atomically unlocks c.L and suspends execution
// of the calling goroutine. After later resuming execution,
// Wait locks c.L before returning. Unlike in other systems,
// Wait cannot return unless awoken by [Cond.Broadcast] or [Cond.Signal].
func (c *Cond) Wait() {
	_ = *c
	rt.AtomicBegin()
	t := c.wait
	c.wait++
	rt.AtomicEnd()
	c.L.Unlock()
	rt.AtomicBeginWhen(t < c.notify)
	rt.AtomicEnd()
	c.L.Lock()
}

// Signal wakes one goroutine waiting on c, if there is any.
func (c *Cond) Signal() {
	_ = *c
	rt.AtomicBegin()
	if c.notify < c.wait {
		c.notify++
	}
	rt.AtomicEnd()
}

// Broadcast wakes all goroutines waiting on c.
func (c *Cond) Broadcast() {
	_ = *c
	rt.AtomicBegin()
	c.notify = c.wait
	rt.AtomicEnd()
}
