// Go1.27 §Handling_panics: "When the running of deferred functions reaches
// D, the return value of D's call to recover will be the value passed to
// the call of panic." "The return value of recover is nil when the
// goroutine is not panicking or recover was not called directly by a
// deferred function." "To ensure this, calling panic with a nil interface
// value (or an untyped nil) causes a run-time panic."
package main

import "cprover"

func value() (v any) {
	defer func() { v = recover() }()
	panic(42)
}

func helper() any { return recover() }

func indirect() (direct, viaHelper any) {
	defer func() { direct = recover() }()
	defer func() { viaHelper = helper() }() // not called directly
	panic("p")
}

// A new panic in a deferred function replaces the first.
func repanic() (v any) {
	defer func() { v = recover() }()
	defer func() { panic("second") }()
	panic("first")
}

func nilValue() (err error) {
	defer func() { err = recover().(error) }()
	panic(nil)
}

// recover itself deferred is not called by a deferred function.
func deferredRecover() (stopped bool) {
	defer func() { stopped = recover() == nil }()
	defer recover()
	panic("not stopped")
}

func twice() (first, second any) {
	defer func() {
		first = recover()
		second = recover()
	}()
	panic("once")
}

func calm() any { return recover() }

func main() {
	cprover.Assert(value() == 42, "the value passed to panic")
	d, h := indirect()
	cprover.Assert(d == "p" && h == nil, "only directly deferred calls recover")
	cprover.Assert(repanic() == "second", "the latest panic")
	cprover.Assert(nilValue().Error() ==
		"runtime error: panic called with nil argument", "panic(nil)")
	cprover.Assert(!deferredRecover(), "defer recover() does not recover")
	f, s := twice()
	cprover.Assert(f == "once" && s == nil, "the panic stops")
	cprover.Assert(calm() == nil, "not panicking")
}
