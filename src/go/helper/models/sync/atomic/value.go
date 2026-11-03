// Copyright 2014 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file of the Go distribution.
// Changes for CBMC's Go front-end: the value is an interface value that
// atomic sections update, and rt.SameType compares dynamic types. The
// comparison and the swap of CompareAndSwap are one atomic step.

package atomic

import "cprover/rt"

// A Value provides an atomic load and store of a consistently typed value.
// The zero value for a Value returns nil from [Value.Load].
// Once [Value.Store] has been called, a Value must not be copied.
type Value struct {
	v any
}

// Load returns the value set by the most recent Store.
// It returns nil if there has been no call to Store for this Value.
func (v *Value) Load() (val any) {
	_ = *v // a nil v panics outside the atomic section
	rt.AtomicBegin()
	val = v.v
	rt.AtomicEnd()
	return
}

// Store sets the value of the [Value] v to val.
// All calls to Store for a given Value must use values of the same concrete type.
// Store of an inconsistent type panics, as does Store(nil).
func (v *Value) Store(val any) {
	if val == nil {
		panic("sync/atomic: store of nil value into Value")
	}
	_ = *v
	rt.AtomicBegin()
	ok := v.v == nil || rt.SameType(v.v, val)
	if ok {
		v.v = val
	}
	rt.AtomicEnd()
	if !ok {
		panic("sync/atomic: store of inconsistently typed value into Value")
	}
}

// Swap stores new into Value and returns the previous value. It returns nil if
// the Value is empty.
//
// All calls to Swap for a given Value must use values of the same concrete
// type. Swap of an inconsistent type panics, as does Swap(nil).
func (v *Value) Swap(new any) (old any) {
	if new == nil {
		panic("sync/atomic: swap of nil value into Value")
	}
	_ = *v
	rt.AtomicBegin()
	old = v.v
	ok := old == nil || rt.SameType(old, new)
	if ok {
		v.v = new
	}
	rt.AtomicEnd()
	if !ok {
		panic("sync/atomic: swap of inconsistently typed value into Value")
	}
	return
}

// CompareAndSwap executes the compare-and-swap operation for the [Value].
//
// All calls to CompareAndSwap for a given Value must use values of the same
// concrete type. CompareAndSwap of an inconsistent type panics, as does
// CompareAndSwap(old, nil).
func (v *Value) CompareAndSwap(old, new any) (swapped bool) {
	if new == nil {
		panic("sync/atomic: compare and swap of nil value into Value")
	}
	if old != nil && !rt.SameType(old, new) {
		panic("sync/atomic: compare and swap of inconsistently typed values")
	}
	_ = *v
	rt.AtomicBegin()
	current := v.v
	if current == nil {
		if old == nil {
			v.v = new
			swapped = true
		}
		rt.AtomicEnd()
		return
	}
	if !rt.SameType(current, new) {
		rt.AtomicEnd()
		panic("sync/atomic: compare and swap of inconsistently typed value into Value")
	}
	// Panics if the dynamic type is not comparable, which ends the
	// atomic section.
	if current == old {
		v.v = new
		swapped = true
	}
	rt.AtomicEnd()
	return
}
