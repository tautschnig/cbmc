// Functions and methods whose signatures have unsupported types are not
// errors: their uses fail a property where they are reached (here, in
// branches that are not taken), so that the rest of the program is
// verified.
package main

import (
	"cprover"
	"unsafe"
)

func raw(p unsafe.Pointer) bool { return p == nil }

type T struct{ n int }

func (t *T) raw(p unsafe.Pointer) bool { return p == nil }

type rawer interface{ raw(p unsafe.Pointer) bool }

// A function value of raw has an unsupported type: the body of viaValue
// fails a property instead.
func viaValue() bool {
	f := raw
	return f(nil)
}

// Calls through an interface value whose method has an unsupported
// signature: the body of viaInterface fails a property instead.
func viaInterface(r rawer) bool { return r.raw(nil) }

func main() {
	n := cprover.Nondet[int]()
	cprover.Assume(0 <= n && n < 100)
	t := &T{n}
	if n > 1000 {
		cprover.Assert(raw(nil), "call")
	}
	if n > 2000 {
		cprover.Assert(viaValue(), "value")
	}
	if n > 3000 {
		cprover.Assert(t.raw(nil), "method")
	}
	if n > 4000 {
		var r rawer = t
		cprover.Assert(viaInterface(r), "interface")
	}
	cprover.Assert(t.n < 100, "n")
}
