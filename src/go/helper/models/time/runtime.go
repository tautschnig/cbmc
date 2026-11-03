// Copyright 2009 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file of the Go distribution.
// Changes for CBMC's Go front-end: package time is the real one (go1.27,
// see sourceModels in models.go) except for the functions that the
// runtime implements and the loading of the local time zone, which are
// these models:
//
//   - The wall clock is arbitrary at each reading (seconds since 1970 in
//     [0, 2^32), as the system clock may be set at any time; Now records
//     the monotonic reading for all of them, as for years 1885 to 2157),
//     and the
//     monotonic clock never decreases. Sleep(d) advances the monotonic
//     clock by at least d.
//   - A timer fires in a goroutine of its own, so at any point of the
//     execution after it is started, unless it is stopped or reset first;
//     firing moves the monotonic clock to at least the timer's time. A
//     periodic timer (Ticker) fires repeatedly. Channel timers buffer one
//     value, as before Go 1.23: a value sent before Stop or Reset may
//     still be received.
//   - The local time zone is a fixed zone with an arbitrary offset, as it
//     depends on the environment (no daylight saving time).

package time

import (
	"cprover"
	"cprover/rt"
	"unsafe"
)

// The clock: whether it has been read, and the monotonic time.
var (
	clockStarted bool
	clockMono    int64
)

// monotonic returns the monotonic clock, which never decreases.
func monotonic() int64 {
	rt.AtomicBegin()
	if !clockStarted {
		clockStarted = true
		clockMono = cprover.Nondet[int64]()
		cprover.Assume(clockMono > 0 && clockMono < 1<<62)
	} else {
		d := cprover.Nondet[int64]()
		cprover.Assume(d >= 0 && d <= 1<<62-clockMono)
		clockMono += d
	}
	mono := clockMono
	rt.AtomicEnd()
	return mono
}

// advance moves the monotonic clock to at least t.
func advance(t int64) {
	monotonic()
	rt.AtomicBegin()
	if clockMono < t {
		clockMono = t
	}
	rt.AtomicEnd()
}

func now() (sec int64, nsec int32, mono int64) {
	sec = cprover.Nondet[int64]()
	cprover.Assume(0 <= sec && sec < 1<<32)
	nsec = cprover.Nondet[int32]()
	cprover.Assume(0 <= nsec && nsec < 1e9)
	return sec, nsec, monotonic()
}

func runtimeNow() (sec int64, nsec int32, mono int64) { return now() }

func runtimeNano() int64 { return monotonic() }

func runtimeIsBubbled() bool { return false }

// Sleep pauses the current goroutine for at least the duration d.
// A negative or zero duration causes Sleep to return immediately.
func Sleep(d Duration) {
	if d <= 0 {
		return
	}
	t := monotonic() + int64(d)
	if t < 0 {
		t = 1<<63 - 1
	}
	advance(t)
}

// The state of a timer (the runtime's timer), allocated with the Timer
// (as the runtime's timeTimer starts with it), so that the state of t is
// at its address.
type timerState struct {
	t      Timer
	f      func(any, uintptr, int64)
	arg    any
	when   int64
	period int64
	active bool
	seq    uintptr
}

func state(t *Timer) *timerState { return (*timerState)(unsafe.Pointer(t)) }

func newTimer(when, period int64, f func(any, uintptr, int64), arg any, cp unsafe.Pointer) *Timer {
	st := &timerState{f: f, arg: arg, when: when, period: period, active: true}
	st.t.initTimer = true
	go fire(st, 0)
	return &st.t
}

// fire runs the timer function of st at its time, unless the timer is
// stopped or reset (to a new sequence number) before.
func fire(st *timerState, seq uintptr) {
	for {
		rt.AtomicBegin()
		ok := st.active && st.seq == seq
		when := st.when
		if ok && st.period <= 0 {
			st.active = false
		}
		rt.AtomicEnd()
		if !ok {
			return
		}
		advance(when)
		st.f(st.arg, seq, 0)
		if st.period <= 0 {
			return
		}
		rt.AtomicBegin()
		st.when += st.period
		rt.AtomicEnd()
	}
}

func stopTimer(t *Timer) bool {
	st := state(t)
	rt.AtomicBegin()
	was := st.active
	st.active = false
	rt.AtomicEnd()
	return was
}

func resetTimer(t *Timer, when, period int64) bool {
	st := state(t)
	rt.AtomicBegin()
	was := st.active
	st.active = true
	st.when = when
	st.period = period
	st.seq++
	seq := st.seq
	rt.AtomicEnd()
	go fire(st, seq)
	return was
}

func initLocal() {
	offset := cprover.Nondet[int]()
	cprover.Assume(-12*60*60 <= offset && offset <= 14*60*60)
	z := FixedZone(cprover.Nondet[string](), offset)
	localLoc = *z
	localLoc.cacheZone = &localLoc.zone[0]
	localLoc.name = "Local"
}
