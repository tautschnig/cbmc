// The model of atomic.Value: loads see the latest store, and stores of
// nil or of values of another type panic.
package main

import (
	"cprover"
	"sync/atomic"
)

func panics(f func()) (msg any) {
	defer func() { msg = recover() }()
	f()
	return nil
}

func main() {
	var v atomic.Value
	cprover.Assert(v.Load() == nil, "empty")
	cprover.Assert(v.CompareAndSwap(nil, 1), "first CompareAndSwap")
	v.Store(2)
	cprover.Assert(v.Load() == 2, "stored")
	cprover.Assert(v.Swap(3) == 2, "Swap")
	cprover.Assert(!v.CompareAndSwap(2, 4), "CompareAndSwap fails")
	cprover.Assert(v.CompareAndSwap(3, 4), "CompareAndSwap swaps")
	cprover.Assert(
		panics(func() { v.Store("s") }) ==
			"sync/atomic: store of inconsistently typed value into Value",
		"inconsistent type")
	cprover.Assert(
		panics(func() { v.Store(nil) }) ==
			"sync/atomic: store of nil value into Value",
		"nil")
	cprover.Assert(
		panics(func() { v.CompareAndSwap(1, "s") }) ==
			"sync/atomic: compare and swap of inconsistently typed values",
		"inconsistent CompareAndSwap")
	cprover.Assert(v.Load() == 4, "unchanged")
}
