// Copyright 2011 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file of the Go distribution.
// Changes for CBMC's Go front-end: each operation is an atomic section of
// the front-end; the documentation is shortened.

// Package atomic models the standard library's package sync/atomic
// (go1.27): the functions on integers, and the types Bool, Int32, Int64,
// Uint32, Uint64, Uintptr and Pointer. The program is type-checked against
// this package, so the functions on unsafe.Pointer and the type Value are
// not available. Operations are sequentially consistent, as the Go memory
// model specifies for them.
package atomic

import "cprover/rt"

// SwapInt32 atomically stores new into *addr and returns the previous *addr
// value.
func SwapInt32(addr *int32, new int32) (old int32) {
	_ = *addr // a nil addr panics outside the atomic section
	rt.AtomicBegin()
	old = *addr
	*addr = new
	rt.AtomicEnd()
	return
}

// CompareAndSwapInt32 executes the compare-and-swap operation for a int32
// value.
func CompareAndSwapInt32(addr *int32, old, new int32) (swapped bool) {
	_ = *addr
	rt.AtomicBegin()
	if *addr == old {
		*addr = new
		swapped = true
	}
	rt.AtomicEnd()
	return
}

// AddInt32 atomically adds delta to *addr and returns the new value.
func AddInt32(addr *int32, delta int32) (new int32) {
	_ = *addr
	rt.AtomicBegin()
	*addr += delta
	new = *addr
	rt.AtomicEnd()
	return
}

// AndInt32 atomically performs a bitwise AND operation on *addr using the
// bitmask provided as mask and returns the old value.
func AndInt32(addr *int32, mask int32) (old int32) {
	_ = *addr
	rt.AtomicBegin()
	old = *addr
	*addr = old & mask
	rt.AtomicEnd()
	return
}

// OrInt32 atomically performs a bitwise OR operation on *addr using the
// bitmask provided as mask and returns the old value.
func OrInt32(addr *int32, mask int32) (old int32) {
	_ = *addr
	rt.AtomicBegin()
	old = *addr
	*addr = old | mask
	rt.AtomicEnd()
	return
}

// LoadInt32 atomically loads *addr.
func LoadInt32(addr *int32) (val int32) {
	_ = *addr
	rt.AtomicBegin()
	val = *addr
	rt.AtomicEnd()
	return
}

// StoreInt32 atomically stores val into *addr.
func StoreInt32(addr *int32, val int32) {
	_ = *addr
	rt.AtomicBegin()
	*addr = val
	rt.AtomicEnd()
}

// SwapInt64 atomically stores new into *addr and returns the previous *addr
// value.
func SwapInt64(addr *int64, new int64) (old int64) {
	_ = *addr // a nil addr panics outside the atomic section
	rt.AtomicBegin()
	old = *addr
	*addr = new
	rt.AtomicEnd()
	return
}

// CompareAndSwapInt64 executes the compare-and-swap operation for a int64
// value.
func CompareAndSwapInt64(addr *int64, old, new int64) (swapped bool) {
	_ = *addr
	rt.AtomicBegin()
	if *addr == old {
		*addr = new
		swapped = true
	}
	rt.AtomicEnd()
	return
}

// AddInt64 atomically adds delta to *addr and returns the new value.
func AddInt64(addr *int64, delta int64) (new int64) {
	_ = *addr
	rt.AtomicBegin()
	*addr += delta
	new = *addr
	rt.AtomicEnd()
	return
}

// AndInt64 atomically performs a bitwise AND operation on *addr using the
// bitmask provided as mask and returns the old value.
func AndInt64(addr *int64, mask int64) (old int64) {
	_ = *addr
	rt.AtomicBegin()
	old = *addr
	*addr = old & mask
	rt.AtomicEnd()
	return
}

// OrInt64 atomically performs a bitwise OR operation on *addr using the
// bitmask provided as mask and returns the old value.
func OrInt64(addr *int64, mask int64) (old int64) {
	_ = *addr
	rt.AtomicBegin()
	old = *addr
	*addr = old | mask
	rt.AtomicEnd()
	return
}

// LoadInt64 atomically loads *addr.
func LoadInt64(addr *int64) (val int64) {
	_ = *addr
	rt.AtomicBegin()
	val = *addr
	rt.AtomicEnd()
	return
}

// StoreInt64 atomically stores val into *addr.
func StoreInt64(addr *int64, val int64) {
	_ = *addr
	rt.AtomicBegin()
	*addr = val
	rt.AtomicEnd()
}

// SwapUint32 atomically stores new into *addr and returns the previous *addr
// value.
func SwapUint32(addr *uint32, new uint32) (old uint32) {
	_ = *addr // a nil addr panics outside the atomic section
	rt.AtomicBegin()
	old = *addr
	*addr = new
	rt.AtomicEnd()
	return
}

// CompareAndSwapUint32 executes the compare-and-swap operation for a uint32
// value.
func CompareAndSwapUint32(addr *uint32, old, new uint32) (swapped bool) {
	_ = *addr
	rt.AtomicBegin()
	if *addr == old {
		*addr = new
		swapped = true
	}
	rt.AtomicEnd()
	return
}

// AddUint32 atomically adds delta to *addr and returns the new value.
func AddUint32(addr *uint32, delta uint32) (new uint32) {
	_ = *addr
	rt.AtomicBegin()
	*addr += delta
	new = *addr
	rt.AtomicEnd()
	return
}

// AndUint32 atomically performs a bitwise AND operation on *addr using the
// bitmask provided as mask and returns the old value.
func AndUint32(addr *uint32, mask uint32) (old uint32) {
	_ = *addr
	rt.AtomicBegin()
	old = *addr
	*addr = old & mask
	rt.AtomicEnd()
	return
}

// OrUint32 atomically performs a bitwise OR operation on *addr using the
// bitmask provided as mask and returns the old value.
func OrUint32(addr *uint32, mask uint32) (old uint32) {
	_ = *addr
	rt.AtomicBegin()
	old = *addr
	*addr = old | mask
	rt.AtomicEnd()
	return
}

// LoadUint32 atomically loads *addr.
func LoadUint32(addr *uint32) (val uint32) {
	_ = *addr
	rt.AtomicBegin()
	val = *addr
	rt.AtomicEnd()
	return
}

// StoreUint32 atomically stores val into *addr.
func StoreUint32(addr *uint32, val uint32) {
	_ = *addr
	rt.AtomicBegin()
	*addr = val
	rt.AtomicEnd()
}

// SwapUint64 atomically stores new into *addr and returns the previous *addr
// value.
func SwapUint64(addr *uint64, new uint64) (old uint64) {
	_ = *addr // a nil addr panics outside the atomic section
	rt.AtomicBegin()
	old = *addr
	*addr = new
	rt.AtomicEnd()
	return
}

// CompareAndSwapUint64 executes the compare-and-swap operation for a uint64
// value.
func CompareAndSwapUint64(addr *uint64, old, new uint64) (swapped bool) {
	_ = *addr
	rt.AtomicBegin()
	if *addr == old {
		*addr = new
		swapped = true
	}
	rt.AtomicEnd()
	return
}

// AddUint64 atomically adds delta to *addr and returns the new value.
func AddUint64(addr *uint64, delta uint64) (new uint64) {
	_ = *addr
	rt.AtomicBegin()
	*addr += delta
	new = *addr
	rt.AtomicEnd()
	return
}

// AndUint64 atomically performs a bitwise AND operation on *addr using the
// bitmask provided as mask and returns the old value.
func AndUint64(addr *uint64, mask uint64) (old uint64) {
	_ = *addr
	rt.AtomicBegin()
	old = *addr
	*addr = old & mask
	rt.AtomicEnd()
	return
}

// OrUint64 atomically performs a bitwise OR operation on *addr using the
// bitmask provided as mask and returns the old value.
func OrUint64(addr *uint64, mask uint64) (old uint64) {
	_ = *addr
	rt.AtomicBegin()
	old = *addr
	*addr = old | mask
	rt.AtomicEnd()
	return
}

// LoadUint64 atomically loads *addr.
func LoadUint64(addr *uint64) (val uint64) {
	_ = *addr
	rt.AtomicBegin()
	val = *addr
	rt.AtomicEnd()
	return
}

// StoreUint64 atomically stores val into *addr.
func StoreUint64(addr *uint64, val uint64) {
	_ = *addr
	rt.AtomicBegin()
	*addr = val
	rt.AtomicEnd()
}

// SwapUintptr atomically stores new into *addr and returns the previous *addr
// value.
func SwapUintptr(addr *uintptr, new uintptr) (old uintptr) {
	_ = *addr // a nil addr panics outside the atomic section
	rt.AtomicBegin()
	old = *addr
	*addr = new
	rt.AtomicEnd()
	return
}

// CompareAndSwapUintptr executes the compare-and-swap operation for a uintptr
// value.
func CompareAndSwapUintptr(addr *uintptr, old, new uintptr) (swapped bool) {
	_ = *addr
	rt.AtomicBegin()
	if *addr == old {
		*addr = new
		swapped = true
	}
	rt.AtomicEnd()
	return
}

// AddUintptr atomically adds delta to *addr and returns the new value.
func AddUintptr(addr *uintptr, delta uintptr) (new uintptr) {
	_ = *addr
	rt.AtomicBegin()
	*addr += delta
	new = *addr
	rt.AtomicEnd()
	return
}

// AndUintptr atomically performs a bitwise AND operation on *addr using the
// bitmask provided as mask and returns the old value.
func AndUintptr(addr *uintptr, mask uintptr) (old uintptr) {
	_ = *addr
	rt.AtomicBegin()
	old = *addr
	*addr = old & mask
	rt.AtomicEnd()
	return
}

// OrUintptr atomically performs a bitwise OR operation on *addr using the
// bitmask provided as mask and returns the old value.
func OrUintptr(addr *uintptr, mask uintptr) (old uintptr) {
	_ = *addr
	rt.AtomicBegin()
	old = *addr
	*addr = old | mask
	rt.AtomicEnd()
	return
}

// LoadUintptr atomically loads *addr.
func LoadUintptr(addr *uintptr) (val uintptr) {
	_ = *addr
	rt.AtomicBegin()
	val = *addr
	rt.AtomicEnd()
	return
}

// StoreUintptr atomically stores val into *addr.
func StoreUintptr(addr *uintptr, val uintptr) {
	_ = *addr
	rt.AtomicBegin()
	*addr = val
	rt.AtomicEnd()
}
