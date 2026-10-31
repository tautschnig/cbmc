// Go1.27 §Defer_statements: "The expression must be a function or method
// call". The receiver is evaluated with the function value (§Method_values),
// and "If a deferred function value evaluates to nil, execution panics when
// the function is invoked, not when the "defer" statement is executed."
package main

import "cprover"

type counter struct{ n int }

func (c *counter) inc()         { c.n++ }
func (c counter) add(d int) int { return c.n + d }

type Closer interface{ Close() }

var closed []string

type file struct{ name string }

func (f file) Close() { closed = append(closed, f.name) }

func pointerReceiver() int {
	c := counter{1}
	func() {
		defer c.inc() // (&c).inc()
	}()
	return c.n
}

func valueReceiver() (got int) {
	c := counter{1}
	defer func() { got = got*100 + c.n }()
	defer func() { got = c.add(0) }()
	defer c.add(1) // c is copied now
	c.n = 9
	return 0
}

func viaInterface() {
	var c Closer = file{"a"}
	defer c.Close()
	c = file{"b"}
	defer c.Close()
}

func nilFunction() (r any, reached bool) {
	defer func() { r = recover() }()
	var g func()
	defer g()
	reached = true
	return
}

// Go1.27 §Selectors: "If x is of interface type and has the value nil,
// calling or evaluating the method x.f causes a run-time panic."
func nilInterface() (r any, reached bool) {
	defer func() { r = recover() }()
	var c Closer
	defer c.Close() // panics here
	reached = true
	return
}

func main() {
	cprover.Assert(pointerReceiver() == 2, "pointer receiver")
	cprover.Assert(valueReceiver() == 909, "value receiver")
	viaInterface()
	cprover.Assert(len(closed) == 2 && closed[0] == "b" && closed[1] == "a",
		"interface method")
	r, reached := nilFunction()
	cprover.Assert(reached && r != nil, "nil function panics when invoked")
	r, reached = nilInterface()
	cprover.Assert(!reached && r != nil, "nil interface panics when deferred")
}
