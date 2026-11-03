// Copyright 2024 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file of the Go distribution.
// Changes for CBMC's Go front-end: the map is a Go map that atomic
// sections update. Range visits each key present when it starts (unless
// it is deleted before), with its value when visited, and then
// nondeterministically keys stored meanwhile, as the documentation allows
// (the real Range may also visit those earlier).

package sync

import (
	"cprover"
	"cprover/rt"
)

// Map is like a Go map[any]any but is safe for concurrent use
// by multiple goroutines without additional locking or coordination.
//
// The zero Map is empty and ready for use. A Map must not be copied after
// first use.
type Map struct {
	m map[any]any
}

// Load returns the value stored in the map for a key, or nil if no
// value is present.
// The ok result indicates whether value was found in the map.
func (m *Map) Load(key any) (value any, ok bool) {
	_ = *m // a nil m panics outside the atomic section
	rt.AtomicBegin()
	value, ok = m.m[key]
	rt.AtomicEnd()
	return
}

// Store sets the value for a key.
func (m *Map) Store(key, value any) {
	m.Swap(key, value)
}

// Clear deletes all the entries, resulting in an empty Map.
func (m *Map) Clear() {
	_ = *m
	rt.AtomicBegin()
	m.m = nil
	rt.AtomicEnd()
}

// LoadOrStore returns the existing value for the key if present.
// Otherwise, it stores and returns the given value.
// The loaded result is true if the value was loaded, false if stored.
func (m *Map) LoadOrStore(key, value any) (actual any, loaded bool) {
	_ = *m
	rt.AtomicBegin()
	actual, loaded = m.m[key]
	if !loaded {
		if m.m == nil {
			m.m = make(map[any]any)
		}
		m.m[key] = value
		actual = value
	}
	rt.AtomicEnd()
	return
}

// LoadAndDelete deletes the value for a key, returning the previous value if any.
// The loaded result reports whether the key was present.
func (m *Map) LoadAndDelete(key any) (value any, loaded bool) {
	_ = *m
	rt.AtomicBegin()
	value, loaded = m.m[key]
	delete(m.m, key)
	rt.AtomicEnd()
	return
}

// Delete deletes the value for a key.
// If the key is not in the map, Delete does nothing.
func (m *Map) Delete(key any) {
	m.LoadAndDelete(key)
}

// Swap swaps the value for a key and returns the previous value if any.
// The loaded result reports whether the key was present.
func (m *Map) Swap(key, value any) (previous any, loaded bool) {
	_ = *m
	rt.AtomicBegin()
	previous, loaded = m.m[key]
	if m.m == nil {
		m.m = make(map[any]any)
	}
	m.m[key] = value
	rt.AtomicEnd()
	return
}

// CompareAndSwap swaps the old and new values for key
// if the value stored in the map is equal to old.
// The old value must be of a comparable type.
func (m *Map) CompareAndSwap(key, old, new any) (swapped bool) {
	_ = *m
	rt.AtomicBegin()
	value, ok := m.m[key]
	if ok && value == old {
		m.m[key] = new
		swapped = true
	}
	rt.AtomicEnd()
	return
}

// CompareAndDelete deletes the entry for key if its value is equal to old.
// The old value must be of a comparable type.
//
// If there is no current value for key in the map, CompareAndDelete
// returns false (even if the old value is the nil interface value).
func (m *Map) CompareAndDelete(key, old any) (deleted bool) {
	_ = *m
	rt.AtomicBegin()
	value, ok := m.m[key]
	if ok && value == old {
		delete(m.m, key)
		deleted = true
	}
	rt.AtomicEnd()
	return
}

// Range calls f sequentially for each key and value present in the map.
// If f returns false, range stops the iteration.
//
// Range does not necessarily correspond to any consistent snapshot of the Map's
// contents: no key will be visited more than once, but if the value for any key
// is stored or deleted concurrently (including by f), Range may reflect any
// mapping for that key from any point during the Range call. Range does not
// block other methods on the receiver; even f itself may call any method on m.
func (m *Map) Range(f func(key, value any) bool) {
	_ = *m
	rt.AtomicBegin()
	keys := make([]any, 0, len(m.m))
	for k := range m.m {
		keys = append(keys, k)
	}
	rt.AtomicEnd()
	// The keys present at the start, unless deleted meanwhile, with their
	// current values.
	for _, k := range keys {
		if v, ok := m.Load(k); ok && !f(k, v) {
			return
		}
	}
	// Keys stored meanwhile (also by f) may be visited too.
	var added []any // keys and values
	rt.AtomicBegin()
	for k, v := range m.m {
		if cprover.Nondet[bool]() && !contains(keys, k) {
			added = append(added, k, v)
		}
	}
	rt.AtomicEnd()
	for i := 0; i < len(added); i += 2 {
		if !f(added[i], added[i+1]) {
			return
		}
	}
}

// contains reports whether keys contains k.
func contains(keys []any, k any) bool {
	for _, key := range keys {
		if key == k {
			return true
		}
	}
	return false
}
