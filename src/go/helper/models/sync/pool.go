// Copyright 2013 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file of the Go distribution.
// Changes for CBMC's Go front-end: the pool is a slice that atomic
// sections update. The real pool may drop any item at any time (garbage
// collection) and may ignore its items in Get, so Put keeps the item or
// not, and Get returns an arbitrary item or none, nondeterministically.

package sync

import (
	"cprover"
	"cprover/rt"
)

// A Pool is a set of temporary objects that may be individually saved and
// retrieved.
//
// Any item stored in the Pool may be removed automatically at any time
// without notification.
type Pool struct {
	items []any

	// New optionally specifies a function to generate
	// a value when Get would otherwise return nil.
	// It may not be changed concurrently with calls to Get.
	New func() any
}

// Put adds x to the pool.
func (p *Pool) Put(x any) {
	if x == nil {
		return
	}
	_ = *p // a nil p panics outside the atomic section
	rt.AtomicBegin()
	if cprover.Nondet[bool]() {
		p.items = append(p.items, x)
	}
	rt.AtomicEnd()
}

// Get selects an arbitrary item from the Pool, removes it from the
// Pool, and returns it to the caller.
// Get may choose to ignore the pool and treat it as empty.
// Callers should not assume any relation between values passed to Put and
// the values returned by Get.
//
// If Get would otherwise return nil and p.New is non-nil, Get returns
// the result of calling p.New.
func (p *Pool) Get() any {
	_ = *p
	var x any
	rt.AtomicBegin()
	if n := len(p.items); n > 0 && cprover.Nondet[bool]() {
		i := cprover.Nondet[int]()
		cprover.Assume(0 <= i && i < n)
		x = p.items[i]
		p.items[i] = p.items[n-1]
		p.items[n-1] = nil
		p.items = p.items[:n-1]
	}
	rt.AtomicEnd()
	if x == nil && p.New != nil {
		x = p.New()
	}
	return x
}

// getFresh is Get for the pools of the standard-library models (see the
// helper's poolShortcut): it ignores the pool, as Get may, so that the
// objects these pools hold are not shared between uses, which would merge
// their states.
func (p *Pool) getFresh() any {
	_ = *p
	if p.New != nil {
		return p.New()
	}
	return nil
}

// putNothing is Put for the pools of the standard-library models (see
// getFresh): it drops x, as the pool may at any time.
func (p *Pool) putNothing(x any) {
	if x == nil {
		return
	}
	_ = *p
}
