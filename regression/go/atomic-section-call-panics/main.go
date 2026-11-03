// A call that panics inside an atomic section of the runtime (as in the
// models of package sync) ends the section while unwinding, as a panic of
// the function itself does.
package main

import (
	"cprover"
	"cprover/rt"
)

func check(n int) {
	if n > 10 {
		panic("large")
	}
}

func update(n int, total *int) {
	rt.AtomicBegin()
	check(n)
	*total += n
	rt.AtomicEnd()
}

func main() {
	n := cprover.Nondet[int]()
	total := 0
	defer func() {
		r := recover()
		cprover.Assert((r != nil) == (n > 10), "panics for large n")
		cprover.Assert(r != nil || total == n, "total")
	}()
	update(n, &total)
}
