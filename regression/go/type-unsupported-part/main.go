// A type with an unsupported part (here, a pointer to a struct with a
// field of type rec) is reported where it is used, also for
// variables on the heap; the rest of the program is verified.
package main

import "cprover"

// A type that refers to itself other than through a struct or a
// function type, which is not supported.
type rec []rec

type box struct{ p rec }

func capture() bool {
	var b *box
	f := func() bool { return b == nil }
	return f()
}

func main() {
	n := cprover.Nondet[int]()
	cprover.Assume(0 <= n && n < 100)
	if n > 1000 {
		cprover.Assert(capture(), "b is nil")
	}
	cprover.Assert(n < 100, "n")
}
